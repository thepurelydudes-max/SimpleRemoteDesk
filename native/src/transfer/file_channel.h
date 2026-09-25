#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace srd::core { class Session; }

namespace srd::transfer {

class FileServer {
public:
    FileServer(std::string password, std::uint16_t port);
    ~FileServer();

    void start();
    void stop() noexcept;

private:
    void run();
    void send_clipboard(class srd::security::SecureSession& secure);
    void receive_clipboard(class srd::security::SecureSession& secure);

    std::string password_;
    std::uint16_t port_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::mutex sessionMutex_;
    core::Session* activeSession_{nullptr};
};

class FileClient {
public:
    FileClient(std::string host, std::string password, std::uint16_t port);

    // Returns number of roots placed into the local clipboard.
    std::size_t download_remote_clipboard();

    // Returns false if the local clipboard has no files.
    bool upload_local_clipboard();

private:
    std::string host_;
    std::string password_;
    std::uint16_t port_;
};

} // namespace srd::transfer
