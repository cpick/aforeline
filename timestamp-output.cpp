#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <unistd.h>
#include <fcntl.h>

#include <algorithm>
#include <iostream>

namespace {

struct FdGuard {
    FdGuard() : dFd(-1) {}
    ~FdGuard() { close(); }
    operator int() const { return dFd; }

    int close() {
        auto closeResult = ::close(dFd);
        dFd = -1;
        return closeResult;
    }

    static void pipeOpen(FdGuard &read, FdGuard &write) {
        enum {
            PIPE_FD_READ = 0
          , PIPE_FD_WRITE
          , PIPE_FD_NUM
        };
        int pipeFd[PIPE_FD_NUM];

        if(pipe(pipeFd)) {
            std::cerr << "pipe failed: " << strerror(errno) << std::endl;
            exit(EXIT_FAILURE);
        }

        read.dFd = pipeFd[PIPE_FD_READ];
        write.dFd = pipeFd[PIPE_FD_WRITE];
    }

    private:
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;
    int dFd;
};

void writeLine(const uint8_t *line, const uint8_t *lineEnd) {
    char prefix[] = "prefix: ";
    write(STDOUT_FILENO, prefix, sizeof(prefix) - 1 /* NUL byte */);
    // TODO return value

    while(line < lineEnd) {
        auto writeResult = write(STDOUT_FILENO, line, lineEnd - line);
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

void writeLines(const uint8_t *buf, const uint8_t *bufEnd) {
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

    FdGuard pipeStdoutFdWrite;
    FdGuard pipeStderrFdWrite;
    {
        FdGuard pipeStdoutFdRead;
        FdGuard pipeStderrFdRead;
        FdGuard::pipeOpen(pipeStdoutFdRead, pipeStdoutFdWrite);
        FdGuard::pipeOpen(pipeStderrFdRead, pipeStderrFdWrite);

        auto pid = fork();
        if(!pid) {
            // child
            pipeStdoutFdWrite.close();
            pipeStderrFdWrite.close();
            close(STDIN_FILENO);

            auto pipeSize = fcntl(pipeStdoutFdRead, F_GETPIPE_SZ);
            while(true) {
                uint8_t buf[pipeSize];
                // TODO poll on both fds

                auto readResult = read(pipeStdoutFdRead, buf, sizeof(buf));
                if(-1 == readResult) {
                    std::cerr << "read failed: " << strerror(errno) << std::endl;
                    return EXIT_FAILURE;
                }

                if(!readResult) {
                    return EXIT_SUCCESS;
                }

                writeLines(buf, buf + readResult);
            }
        }
    }

    // redirect stdout and stderr

    if(-1 == dup2(pipeStdoutFdWrite, STDOUT_FILENO)) {
        std::cerr << "dup2 stdout failed: " << strerror(errno) << std::endl;
        return EXIT_FAILURE;
    }
    pipeStdoutFdWrite.close();

    if(-1 == dup2(pipeStderrFdWrite, STDERR_FILENO)) {
        std::cerr << "dup2 stderr failed: " << strerror(errno) << std::endl;
        return EXIT_FAILURE;
    }
    pipeStderrFdWrite.close();

    // execute command
    execvp(argv[ARGV_COMMAND], &argv[ARGV_COMMAND]);
    std::cerr << "execvp failed: " << strerror(errno) << std::endl;
    return EXIT_FAILURE;
}
