#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <unistd.h>
#include <fcntl.h>

#include <algorithm>
#include <iostream>
#include <sstream>

namespace {

// takes ownership of fd
class Endpoint {
public:
    explicit Endpoint(int &fd) : dFd(fd) { fd = -1; }
    Endpoint(Endpoint &&old) : dFd(old.dFd) { old.dFd = -1; };
    ~Endpoint() { if(-1 != dFd) close(dFd); }

private:
    Endpoint(const Endpoint&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;

protected:
    int dFd;
};

class Reader: public Endpoint {
    using Endpoint::Endpoint;

public:
    typedef std::vector<uint8_t> Data;
    Data read() {
        auto pipeSize = fcntl(dFd, F_GETPIPE_SZ);
        if(-1 == pipeSize) {
            std::stringstream error;
            error << "fcntl F_GETPIPE_SZ failed: " << strerror(errno);
            throw std::runtime_error(error.str());
        }

        Data data(pipeSize);

        auto readResult = ::read(dFd, data.data(), data.size());
        if(-1 == readResult) {
            std::stringstream error;
            error << "read failed: " << strerror(errno);
            throw std::runtime_error(error.str());
        }

        data.resize(readResult);
        return data;
    }
};

class Writer: public Endpoint {
    using Endpoint::Endpoint;

public:
    void dup2(int fdNew) {
        if(-1 == ::dup2(dFd, fdNew)) {
            std::stringstream error;
            error << "dup2 to new fd: " << fdNew << " failed: " << strerror(errno);
            throw std::runtime_error(error.str());
        }
    }
};

class Pipe {
public:
    Pipe() {
        if(pipe(dFds.data())) {
            std::stringstream error;
            error << "pipe failed: " << strerror(errno);
            throw std::runtime_error(error.str());
        }
    }

    ~Pipe() { close(); }

    Reader useAsReader() {
        Reader reader(dFds[PIPE_FD_READ]);
        close();
        return reader;
    }

    Writer useAsWriter() {
        Writer writer(dFds[PIPE_FD_WRITE]);
        close();
        return writer;
    }

private:
    // non-copyable
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;

    void close() {
        for(auto &fd: dFds) {
            if(-1 == fd) continue;
            ::close(fd); // TODO do anthing with return value?
            fd = -1;
        }
    }

    enum {
        PIPE_FD_READ = 0
      , PIPE_FD_WRITE
      , PIPE_FD_NUM
    };
    std::array<int, PIPE_FD_NUM> dFds;
};

template<typename ContinguousIt>
void writeLine(ContinguousIt line, ContinguousIt lineEnd) {
    {
#define TIME_SUFFIX ": "
        char timeBuf[sizeof("yyyy-mm-ddThh:mm:ssZ" TIME_SUFFIX)];

        auto t = std::time(nullptr);
        auto timeLength = std::strftime(timeBuf, sizeof(timeBuf), "%FT%TZ" TIME_SUFFIX, std::gmtime(&t));
#undef TIME_SUFFIX

        if(!timeLength) {
            std::cerr << "strftime failed" << std::endl;
            exit(EXIT_FAILURE);
        }

        write(STDOUT_FILENO, timeBuf, timeLength);
        // TODO return value
    }

    while(line < lineEnd) {
        auto writeResult = write(STDOUT_FILENO, &*line, std::distance(line, lineEnd));
        if(-1 == writeResult) {
            std::cerr << "write failed: " << strerror(errno) << std::endl;
            exit(EXIT_FAILURE);
        }

        line += writeResult;
    }

    char suffix[] = "\n";
    write(STDOUT_FILENO, suffix, sizeof(suffix) - 1 /* NUL byte */);
    // TODO return value
}

template<typename ContinguousIt>
void writeLines(ContinguousIt buf, ContinguousIt bufEnd) {
    while(buf < bufEnd) {
        auto findResult = std::find(buf, bufEnd, '\n');
        writeLine(buf, findResult);
        buf = findResult;
        if(buf == bufEnd) {
            uint8_t epilog[] = "[logging: interrupted line]";
            writeLine(epilog, epilog + sizeof(epilog));
            break;
        }
        ++buf;
    }

    uint8_t epilog[] = "[logging: finished cleanly]";
    writeLine(epilog, epilog + sizeof(epilog));
}

}

int main(int argc, char *argv[]) {
    enum {
        ARGV_NAME = 0
      , ARGV_COMMAND
      , ARGV_NUM_MIN
    };
    if(argc < ARGV_NUM_MIN) {
        std::cerr << "too few arguments" << std::endl;
        return EXIT_FAILURE;
    }

    Pipe pipe;

    if(!fork()) { // child
        auto reader = pipe.useAsReader();
        close(STDIN_FILENO);

        while(true) {
            auto data = reader.read();
            if(data.empty()) return EXIT_SUCCESS;
            writeLines(std::begin(data), std::end(data));
        }
    }

    // redirect stdout and stderr
    {
        auto writer = pipe.useAsWriter();
        for(const auto fd: { STDOUT_FILENO, STDERR_FILENO }) {
            writer.dup2(fd);
        }
    }

    // execute command
    execvp(argv[ARGV_COMMAND], &argv[ARGV_COMMAND]);
    std::cerr << "execvp failed: " << strerror(errno) << std::endl;
    return EXIT_FAILURE;
}
