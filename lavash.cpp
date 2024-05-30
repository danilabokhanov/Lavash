#include <unistd.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/wait.h>
#include <iostream>

const long CAOS_NUMBER = 1984;

struct Command {
    std::string name;
    std::vector<std::string> args;
    std::optional<std::string> in_file;
    std::optional<std::string> out_file;
};

enum Op {
    PIPE,
    IN,
    OUT,
    AND,
    OR,
    END,
    STR
};

std::vector<std::vector<Command>> pipelines;
std::vector<Op> ops;

struct Token {
    Op op = STR;
    std::optional<std::string> name;
};

Token ScanNextToken(char* str) {
    static size_t it;
    for (; str[it] && isspace(str[it]); it++) {}
    if (!str[it]) {
        return {Op::END, std::nullopt};
    }
    if (str[it] == '"') {
        it++;
        std::string res;
        for (; str[it] && str[it] != '"'; it++) {
            if (str[it] == '\\') {
                it++;
            }
            res += str[it];
        }
        if (str[it]) {
            it++;
        }
        return {Op::STR, res};
    }
    if (str[it] == '<') {
        it++;
        return {Op::IN,  std::nullopt};
    }
    if (str[it] == '>') {
        it++;
        return {Op::OUT,  std::nullopt};
    }
    if (str[it] == '&' && str[it + 1] == '&') {
        it+= 2;
        return {Op::AND,  std::nullopt};
    }
    if (str[it] == '|' && str[it + 1] == '|') {
        it+= 2;
        return {Op::OR,  std::nullopt};
    }
    if (str[it] == '|') {
        it++;
        return {Op::PIPE,  std::nullopt};
    }
    std::string res;
    for (; str[it] && !isspace(str[it]); it++) {
        if (str[it] == '\\') {
            it++;
        }
        res += str[it];
    }
    return {Op::STR, res};
}

void ParseData(char* str) {
    std::vector<Command> pipeline;
    Command command;
    for (Token token = ScanNextToken(str); token.op != END; token = ScanNextToken(str)) {
        switch (token.op) {
            case IN:
                command.in_file = ScanNextToken(str).name;
                break;
            case OUT:
                command.out_file = ScanNextToken(str).name;
                break;
            case STR:
                if (command.name.empty()) {
                    command.name = token.name.value();
                } else {
                    command.args.push_back(token.name.value());
                }
                break;
            case PIPE:
                pipeline.push_back(std::move(command));
                command = {};
                break;
            case AND:
                pipeline.push_back(std::move(command));
                command = {};
                pipelines.push_back(std::move(pipeline));
                pipeline = {};
                ops.push_back(AND);
                break;
            case OR:
                pipeline.push_back(std::move(command));
                command = {};
                pipelines.push_back(std::move(pipeline));
                pipeline = {};
                ops.push_back(OR);
                break;
            case END:
                break;
        }
    }
    pipeline.push_back(std::move(command));
    pipelines.push_back(std::move(pipeline));
}

void custom_exec(const char * cm, char * const *args) {
    if (!*cm) {
        exit(0);
    }
    if (strtol(cm, nullptr, 10) == CAOS_NUMBER) {
        char buf;
        while (read(0, &buf, sizeof (buf)));
        exit(0);
    }
    execvp(cm, args);
}

void close_pipes(std::vector<int[2]>& pipes) {
    for (auto & pipe : pipes) {
        close(pipe[0]);
        close(pipe[1]);
    }
}

void ExecPipeline(const std::vector<Command>& pipeline, bool and_flag, int& last_res) {
    if ((and_flag && last_res) || (!and_flag && !last_res)) {
        return;
    }
    std::vector<int[2]> pipes(pipeline.size() - 1);
    for (size_t i = 0; i < pipeline.size() - 1; i++) {
        pipe(pipes[i]);
    }
    std::vector<pid_t> pids(pipeline.size());
    std::vector<int> statuses(pipeline.size());
    for (size_t i = 0; i < pipeline.size(); i++) {
        pid_t pd = fork();
        if (!pd) {
            if (pipeline[i].in_file.has_value()) {
                int fd = open(pipeline[i].in_file->data(), O_RDONLY);
                if (fd < 0) {
                    perror(("./lavash: line 1: " + pipeline[i].in_file.value()).data());
                    exit(1);
                }
                dup2(fd, 0);
                close(fd);
            } else if (i >= 1) {
                dup2(pipes[i - 1][0], 0);
            }
            if (pipeline[i].out_file.has_value()) {
                int fd = open(pipeline[i].out_file->data(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
                dup2(fd, 1);
                close(fd);
            } else if (i + 1 < pipeline.size()) {
                dup2(pipes[i][1], 1);
            }
            close_pipes(pipes);
            std::vector<char*> ptrs;
            ptrs.push_back(const_cast<char*>(pipeline[i].name.data()));
            for (const auto& arg : pipeline[i].args) {
                ptrs.push_back(const_cast<char*>(arg.data()));
            }
            ptrs.push_back(nullptr);
            custom_exec(const_cast<char*>(pipeline[i].name.data()), ptrs.data());
            std::cerr << "./lavash: line 1: " << pipeline[i].name << ": command not found\n";
            exit(127);
        }
        pids[i] = pd;
    }

    close_pipes(pipes);
    for (size_t i = 0; i < pipeline.size(); i++) {
        waitpid(pids[i], &statuses[i], 0);
    }
    last_res = WEXITSTATUS(statuses.back());
}

void ExecAll() {
    int last_res = 0;
    ExecPipeline(pipelines[0], true, last_res);
    for (size_t i = 1; i < pipelines.size(); i++) {
        ExecPipeline(pipelines[i], ops[i - 1] == AND, last_res);
    }
    exit(last_res);
}

int main(int argc, char **argv, char **envv) {
    ParseData(argv[2]);
    ExecAll();
}
