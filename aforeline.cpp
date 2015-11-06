#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

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
            error << "fcntl F_GETPIPE_SZ failed: " << errno << " " << strerror(errno);
            throw std::runtime_error(error.str());
        }

        Data data(pipeSize);

        while(true) {
            auto readResult = ::read(dFd, data.data(), data.size());
            if(-1 == readResult) {
                if(EINTR == errno) continue;

                std::stringstream error;
                error << "read failed: " << errno << " " << strerror(errno);
                throw std::runtime_error(error.str());
            }

            data.resize(readResult);
            return data;
        }
    }
};

class Writer: public Endpoint {
    using Endpoint::Endpoint;

public:
    void dup2(int fdNew) {
        if(-1 == ::dup2(dFd, fdNew)) {
            std::stringstream error;
            error << "dup2 to new fd: " << fdNew << " failed: " << errno << " " << strerror(errno);
            throw std::runtime_error(error.str());
        }
    }
};

class Pipe {
public:
    Pipe() {
        if(pipe(dFds.data())) {
            std::stringstream error;
            error << "pipe failed: " << errno << " " << strerror(errno);
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
void writeRetry(ContinguousIt buf, ContinguousIt bufEnd) {
    while(buf < bufEnd) {
        auto writeResult = write(STDOUT_FILENO, &*buf, std::distance(buf, bufEnd));
        if(-1 == writeResult) {
            if(EINTR == errno) continue;

            // TODO does it make sense to handle EPIPE differently?
            std::stringstream error;
            error << "write failed: " << errno << " " << strerror(errno);
            throw std::runtime_error(error.str());
        }

        buf += writeResult;
    }
}

template<typename ContinguousIt1, typename ContinguousIt2>
void writeLine(ContinguousIt1 prefix, ContinguousIt1 prefixEnd,
        ContinguousIt2 line, ContinguousIt2 lineEnd) {
    writeRetry(prefix, prefixEnd);

    writeRetry(line, lineEnd);

    static char SUFFIX[] = "\n";
    writeRetry(SUFFIX, SUFFIX + (sizeof(SUFFIX) - 1 /* NUL byte */));
}

template<typename ContinguousIt>
void writeLines(ContinguousIt buf, ContinguousIt bufEnd) {
    // make note of the timestamp once
#define TIME_SUFFIX ": "
    std::array<char, sizeof("yyyy-mm-ddThh:mm:ssZ" TIME_SUFFIX)> timeBuf;
    {
        auto t = std::time(nullptr);
        auto timeLength = std::strftime(timeBuf.data(), timeBuf.size(), "%FT%TZ" TIME_SUFFIX, std::gmtime(&t));
#undef TIME_SUFFIX

        if((timeBuf.size() - 1 /* NUL byte */) != timeLength) {
            std::stringstream error;
            error << "strftime failed: " << timeLength;
            throw std::runtime_error(error.str());
        }
    }
    auto timeBufBegin = std::begin(timeBuf);
    auto timeBufEnd = std::end(timeBuf) - 1 /* NUL byte */;

    while(buf < bufEnd) {
        auto findResult = std::find(buf, bufEnd, '\n');
        writeLine(timeBufBegin, timeBufEnd, buf, findResult);
        buf = findResult;
        if(buf == bufEnd) {
            const uint8_t epilog[] = "[logging: interrupted line]";
            writeLine(timeBufBegin, timeBufEnd, std::begin(epilog), std::end(epilog) - 1 /* NUL byte */);
            break;
        }
        ++buf;
    }
}

void signalsIgnore()
{
    struct sigaction sigaction_new = {};
    sigaction_new.sa_flags = SA_RESETHAND;
    sigaction_new.sa_handler = SIG_IGN;

    for(const auto signal: {
            SIGHUP
          , SIGINT
          , SIGQUIT
          , SIGPIPE
          , SIGTERM
          , SIGUSR1
          , SIGUSR2
            }) {
        if(sigaction(signal, &sigaction_new, NULL /* sigactions_orig */))
        {
            std::stringstream error;
            error << "sigaction signal: " << signal << " " << strsignal(signal) << " failed: " << errno << " " << strerror(errno);
            throw std::runtime_error(error.str());
        }
    }
}

void writeStatusLine(const char status[])
{
    // latch these values as early as possible
    // hopefully catching our original parent while it's alive before we're a child of init
    static const auto PARENT_PID = getppid();
    static const auto PID = getpid();

    std::ostringstream lineStream;
    lineStream << "[logging: " << status << " from pid: " << PARENT_PID << " logged by pid: " << PID << "]\n";
    auto line = lineStream.str();
    writeLines(std::begin(line), std::end(line));
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
        signalsIgnore();

        auto reader = pipe.useAsReader();
        close(STDIN_FILENO);

        writeStatusLine("started");

        while(true) {
            auto data = reader.read();
            if(data.empty()) {
                writeStatusLine("finished cleanly");
                return EXIT_SUCCESS;
            }
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
    std::cerr << "execvp failed: " << errno << " " << strerror(errno) << std::endl;
    return EXIT_FAILURE;
}
