using System.Buffers;
using System.Buffers.Binary;
using System.Security.Cryptography;

namespace RemoteViewer;

internal sealed class SecurePacket : IDisposable
{
    private byte[]? _buffer;
    public int Length { get; }

    public SecurePacket(byte[] buffer, int length)
    {
        _buffer = buffer;
        Length = length;
    }

    public byte Type => _buffer != null && Length > 0 ? _buffer[0] : (byte)0;
    public byte[] Buffer => _buffer ?? throw new ObjectDisposedException(nameof(SecurePacket));
    public int PayloadOffset => 1;
    public int PayloadLength => Math.Max(0, Length - 1);
    public ReadOnlySpan<byte> PayloadSpan => Buffer.AsSpan(PayloadOffset, PayloadLength);

    public void Dispose()
    {
        byte[]? buffer = Interlocked.Exchange(ref _buffer, null);
        if (buffer != null)
            ArrayPool<byte>.Shared.Return(buffer);
    }
}

internal sealed class SecureChannel : IDisposable
{
    private readonly Stream _stream;
    private readonly byte[] _key;
    private readonly uint _sendPrefix;
    private readonly uint _receivePrefix;
    private long _sendSequence;
    private readonly SemaphoreSlim _sendLock = new(1, 1);
    private int _disposed;

    public SecureChannel(Stream stream, byte[] key, uint sendPrefix, uint receivePrefix)
    {
        _stream = stream;
        _key = key;
        _sendPrefix = sendPrefix;
        _receivePrefix = receivePrefix;
    }

    public Task SendAsync(byte type, ReadOnlyMemory<byte> payload, CancellationToken ct) =>
        SendPartsAsync(type, payload, ReadOnlyMemory<byte>.Empty, ct);

    public async Task SendPartsAsync(byte type, ReadOnlyMemory<byte> first, ReadOnlyMemory<byte> second, CancellationToken ct)
    {
        if (Volatile.Read(ref _disposed) != 0) throw new ObjectDisposedException(nameof(SecureChannel));
        await _sendLock.WaitAsync(ct).ConfigureAwait(false);
        byte[]? plaintext = null;
        byte[]? ciphertext = null;
        try
        {
            int length = checked(1 + first.Length + second.Length);
            if (length <= 0 || length > 64 * 1024 * 1024)
                throw new InvalidDataException("Packet too large.");

            plaintext = ArrayPool<byte>.Shared.Rent(length);
            ciphertext = ArrayPool<byte>.Shared.Rent(length);
            plaintext[0] = type;
            first.Span.CopyTo(plaintext.AsSpan(1, first.Length));
            second.Span.CopyTo(plaintext.AsSpan(1 + first.Length, second.Length));

            long sequence = Interlocked.Increment(ref _sendSequence);
            byte[] nonce = new byte[12];
            BinaryPrimitives.WriteUInt32LittleEndian(nonce.AsSpan(0, 4), _sendPrefix);
            BinaryPrimitives.WriteInt64LittleEndian(nonce.AsSpan(4, 8), sequence);
            byte[] tag = new byte[16];
            byte[] aad = new byte[8];
            BinaryPrimitives.WriteInt64LittleEndian(aad, sequence);

            using (var aes = new AesGcm(_key, 16))
                aes.Encrypt(nonce, plaintext.AsSpan(0, length), ciphertext.AsSpan(0, length), tag, aad);

            byte[] header = new byte[28];
            BinaryPrimitives.WriteInt32LittleEndian(header.AsSpan(0, 4), length);
            BinaryPrimitives.WriteInt64LittleEndian(header.AsSpan(4, 8), sequence);
            tag.CopyTo(header, 12);

            await _stream.WriteAsync(header, ct).ConfigureAwait(false);
            await _stream.WriteAsync(ciphertext.AsMemory(0, length), ct).ConfigureAwait(false);
        }
        finally
        {
            if (plaintext != null) ArrayPool<byte>.Shared.Return(plaintext);
            if (ciphertext != null) ArrayPool<byte>.Shared.Return(ciphertext);
            _sendLock.Release();
        }
    }

    public async Task<SecurePacket> ReceiveAsync(CancellationToken ct)
    {
        byte[] header = await ReadExactlyNewAsync(28, ct).ConfigureAwait(false);
        int length = BinaryPrimitives.ReadInt32LittleEndian(header.AsSpan(0, 4));
        long sequence = BinaryPrimitives.ReadInt64LittleEndian(header.AsSpan(4, 8));
        if (length <= 0 || length > 64 * 1024 * 1024)
            throw new InvalidDataException("Invalid packet length.");

        byte[] ciphertext = ArrayPool<byte>.Shared.Rent(length);
        byte[] plaintext = ArrayPool<byte>.Shared.Rent(length);
        try
        {
            await ReadExactlyIntoAsync(ciphertext.AsMemory(0, length), ct).ConfigureAwait(false);

            byte[] nonce = new byte[12];
            BinaryPrimitives.WriteUInt32LittleEndian(nonce.AsSpan(0, 4), _receivePrefix);
            BinaryPrimitives.WriteInt64LittleEndian(nonce.AsSpan(4, 8), sequence);
            byte[] aad = new byte[8];
            BinaryPrimitives.WriteInt64LittleEndian(aad, sequence);

            using (var aes = new AesGcm(_key, 16))
                aes.Decrypt(nonce, ciphertext.AsSpan(0, length), header.AsSpan(12, 16), plaintext.AsSpan(0, length), aad);

            if (length < 1) throw new InvalidDataException("Empty packet.");
            byte[] result = plaintext;
            plaintext = null!;
            return new SecurePacket(result, length);
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(ciphertext);
            if (plaintext != null) ArrayPool<byte>.Shared.Return(plaintext);
        }
    }

    private async Task<byte[]> ReadExactlyNewAsync(int count, CancellationToken ct)
    {
        byte[] buffer = new byte[count];
        await ReadExactlyIntoAsync(buffer, ct).ConfigureAwait(false);
        return buffer;
    }

    private async Task ReadExactlyIntoAsync(Memory<byte> destination, CancellationToken ct)
    {
        int offset = 0;
        while (offset < destination.Length)
        {
            int read = await _stream.ReadAsync(destination.Slice(offset), ct).ConfigureAwait(false);
            if (read == 0) throw new EndOfStreamException();
            offset += read;
        }
    }

    public void Dispose()
    {
        // Не освобождаем SemaphoreSlim и не затираем ключ здесь: Send/Receive могут
        // завершаться одновременно при разрыве TCP. Процесс всё равно освобождает
        // эти короткоживущие объекты после завершения сессии.
        Interlocked.Exchange(ref _disposed, 1);
    }
}
