#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <string>
#include <iostream>
#include <sstream>
#include <iomanip>

#define MSG_TYPE_QUERY    1      // 查询
#define MSG_TYPE_DOWNLOAD 2      // 下载
#define MSG_TYPE_UPLOAD   3      // 上传
#define MSG_TYPE_ERROR    4      // 错误

#define BUFFER_SIZE       1024   // buffer最大大小
#define NAME_SIZE         50     // 文件名(路径)最大大小

const char QUERY_PATH[] = ""; // 查询路径(网盘的相对路径)
const char DOWNLOAD_PATH[] = "/home/draft/Clion/linux/client/download/"; // 下载路径(客户的绝对路径)
const char UPLOAD_PATH[] = "/home/draft/Clion/linux/client/upload/"; // 上传路径(客户端的绝对路径)

/**
 * 1: Hint
 * 2: Error
 * 3: Warn
 * 4: Info
 * 5: Debug
 */
const int log_level = 5;

// * \033[0m：重置所有颜色和样式设置。
// * \033[1;32m：设置文本颜色为绿色。:Hint
// * \033[1;31m：设置文本颜色为红色。:Error
// * \033[1;33m：设置文本颜色为黄色。:Warn
// * \033[1;34m：设置文本颜色为蓝色。:Log Debug
// * \033[1;35m：设置文本颜色为紫色。:Info
// * \033[1;36m：设置文本颜色为青色。
// * \033[1;37m：设置文本颜色为白色。:input

// 输出提示语
void output_hint(const std::string &hint) { // green, always output
    if (!(log_level >= 1)) return;
    std::cout << "\033[1;32m" << "[Hint] " << hint << "\033[0m" << std::endl << std::flush;
}

// 输出错误
void output_error(const std::string &hint) { // red
    if (!(log_level >= 2)) return;
    std::cout << "\033[1;31m" << hint << ": perror=" << std::flush;
    perror("");
    std::cout << "\033[0m" << std::flush << "\033[0m" << std::flush;
}

// 输出警告
void output_warn(const std::string &hint) { // yellow
    if (!(log_level >= 3)) return;
    std::cout << "\033[1;33m" << "[Warn] " << hint << "\033[0m" << std::endl << std::flush;
}

// 输出运行信息
void output_info(const std::string &hint) { // purple
    if (!(log_level >= 4)) return;
    std::cout << "\033[1;35m" << "[Info] " << hint << "\033[0m" << std::endl << std::flush;
}

// 输出调试信息
void output_debug(const std::string &hint) { // blue
    if (!(log_level >= 5)) return;
    std::cout << "\033[1;34m" << "[Debug] " << hint << "\033[0m" << std::endl << std::flush;
}

// 输出提示，并要求输入(类似python的input)
std::string input_with_hint(const std::string &hint = "") {
    output_hint(hint);
    std::string input;
    std::cin >> input;
    return input;
}

// UI
void net_disk_ui() {
//    system("clear");
    output_hint("----Function Menu----");
    output_hint("\t1.query");
    output_hint("\t2.download");
    output_hint("\t3.upload");
    output_hint("\t4.refresh");
    output_hint("\t5.exit");
    output_hint("----Please select----");
}

// buffer转string(转为十六进制显示)
std::string buffer_to_string(char *buffer, size_t n) {
    std::string tmp;
    for (int i = 0; i < n; ++i) {
        tmp += "\\0x";
        tmp += (std::stringstream() << std::hex << std::setw(2) << std::setfill('0') << int(uint8_t(buffer[i]))).str();
    }
    return tmp;
}

// 用于传递信息
typedef struct msg {
    int type;
    int flag;
    char buffer[BUFFER_SIZE];
    char fname[NAME_SIZE];
    int bytes;

    // 用于输出信息
    std::string toString() {
        std::stringstream ss;
        auto buffer_copy = buffer_to_string(buffer, bytes);
        if (type == -1) {
            output_warn("get empty msg");
        }
        if (type == 0) {
            output_warn("get empty msg");
        }
        ss << "msg{"
           << ".type=" << type << ", "
           << ".flag=" << flag << ", "
           << ".buffer=" << R"(")" << buffer_copy << R"(")" << ", "
           << ".fname=" << R"(")" << fname << R"(")" << ", "
           << ".bytes=" << bytes << "}";
        return ss.str();
    }

    // 清空结构体
    void clear() {
        type = -1;
        flag = 0;
        memset(buffer, 0, sizeof buffer);
        memset(fname, 0, sizeof fname);
        bytes = 0;
    }
} MSG;

// 向socket中写入(发送)信息，同时输出log
ssize_t read_net_with_log(int socket, MSG *m, size_t n, const std::string &hint) {
    ssize_t res = read(socket, m, n);
    output_debug("client <= " + m->toString() + " (" + std::to_string(res) + " bytes)" + " (" + hint + ")");
    return res;
}

// 从socket中读取(发送)信息，同时输出log
ssize_t write_net_with_log(int socket, MSG *m, size_t n, const std::string &hint) {
    ssize_t res = write(socket, m, n);
    output_debug("client => " + m->toString() + " (" + std::to_string(res) + " bytes)" + " (" + hint + ")");
    return res;
}

// 从文件中读取信息，同时输出log
ssize_t read_file_with_log(int fd, char *buffer, size_t n, const std::string &hint) {
    ssize_t res = read(fd, buffer, n);
    output_debug(
            "file => " + buffer_to_string(buffer, res) + " (" + std::to_string(res) + " bytes)" + " (" + hint + ")");
    return res;
}

// 向文件中写入信息，同时输出log
ssize_t write_file_with_log(int fd, char *buffer, size_t n, const std::string &hint) {
    ssize_t res = write(fd, buffer, n);
    output_debug(
            "file <= " + buffer_to_string(buffer, res) + " (" + std::to_string(res) + " bytes)" + " (" + hint + ")");
    return res;
}

// 初始化client_socket
int init_client_socket() {
    int client_socket;
    // 创建套接字
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        output_error("fail to create socket");
        return client_socket;
    }
    // 设置ip地址和端口
    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("120.46.38.26");// 本机可以127.0.0.1
    server_addr.sin_port = htons(6667);
    // 创建连接
    if (connect(client_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        output_error("fail to connect server!");
        return client_socket;
    }
    output_info("finish connecting server!");
    return client_socket;
}

// 接收专用线程
void *thread_receive(void *arg) {
    static int fd = -1;
    int client_socket = *((int *) arg);
    MSG receive_msg = {0};
    ssize_t res;
    std::string downloading_file;

    for (receive_msg.clear(); true; receive_msg.clear()) {
        res = read_net_with_log(client_socket, &receive_msg, sizeof(MSG), "switching");
        if (res == 0) {
            output_info("server finished connection");
            break;
        }
        switch (receive_msg.type) {
            case MSG_TYPE_QUERY:
                output_info("query result: filename = " + std::string(receive_msg.fname));
                break;
            case MSG_TYPE_DOWNLOAD:
                // 判断文件夹存在情况
                if (mkdir(DOWNLOAD_PATH, S_IRWXU) < 0) {
                    if (errno == EEXIST) {
//                        output_info("dir exist");
                    } else {
                        output_error("fail to make dir");
                        return nullptr;
                    }
                } else {
                    output_warn("dir no exist, auto created");
                }
                // 打开文件
                if (fd == -1) {
                    downloading_file = std::string(DOWNLOAD_PATH) + receive_msg.fname;
                    fd = open(downloading_file.c_str(), O_CREAT | O_WRONLY, 0666);
                    if (fd < -1) {
                        output_error("can't open file: " + downloading_file);
                        fd = -1;
                        downloading_file = "";
                        break;
                    }
                    // 开始下载(第一次写入)
                    output_info("downloading file: " + downloading_file);
                }
                // 防止多线程下载冲突
                if (downloading_file != std::string(DOWNLOAD_PATH) + receive_msg.fname) {
                    output_error("can't download file, wait for:" + downloading_file + "'s download");
                    break;
                }
                // 写入文件
                res = write_file_with_log(fd, receive_msg.buffer, receive_msg.bytes, "downloading, writing to file");
                // 写入失败
                if (res < 0) {
                    output_error("fail to write file");
                    close(fd);
                    fd = -1;
                    downloading_file = "";
                }
                // 下载完成(最后一次写入)
                if (receive_msg.bytes < sizeof(receive_msg.buffer)) {
                    output_info("downloaded file: " + downloading_file);
                    close(fd);
                    fd = -1;
                    downloading_file = "";
                }
                break;
            case MSG_TYPE_UPLOAD:
                output_error("this branch is theoretically impossible to enter");
                return nullptr;
            case MSG_TYPE_ERROR:
                output_error(receive_msg.fname);
                break;
            default:
                output_error("unknown type" + std::to_string(receive_msg.type));
                return nullptr;
        }
    }
    return nullptr;
}

// 查询函数
void func_query(int client_socket, MSG &send_msg) {
    ssize_t res;
    std::string input;

    send_msg.type = MSG_TYPE_QUERY;
    input = input_with_hint("please input the dir_path(relative path) to query(\"./\"=root)");
    if (*--input.end() != '/') {
        output_error("dir_path should be end with '/': " + input);
        return;
    }
    if (input == "./") input = "";
    memcpy(send_msg.fname, (QUERY_PATH + input).c_str(), input.length());
    output_info("query dir_path: " + input);
    res = write_net_with_log(client_socket, &send_msg, sizeof(MSG), "ask for file name");
    if (res < 0) {
        output_error("fail to send msg");
    }

}

// 下载函数
void func_download(int client_socket, MSG &send_msg) {
    ssize_t res;
    std::string input;

    send_msg.type = MSG_TYPE_DOWNLOAD;
    input = input_with_hint("please input the file_path(relative path) to download");
    memcpy(send_msg.fname, input.c_str(), input.length());
    output_info("downloading file_path: " + input);
    res = write_net_with_log(client_socket, &send_msg, sizeof(MSG), "ask for download");
    if (res < 0) {
        output_error("fail to send msg");
    }
}

// 上传线程的封装参数
struct param_upload {
    int client_socket;
    char *upload_to_path;
    char *upload_from_path;
};

// 上传线程
void *thread_upload(/*@ShouldBeFree*/ void *arg) {
    auto p = (param_upload *) arg;

    MSG up_file_msg = {0};
    static int fd = -1;
    ssize_t res = 0;
    char buffer[BUFFER_SIZE] = {0};
    fd = open(p->upload_from_path, O_RDONLY);
    if (fd < 0) {
        output_error("fail to open file" + std::string(p->upload_from_path));
        free(p->upload_to_path);
        free(p->upload_from_path);
        free(p);
        return nullptr;
    }

    // 循环上传
    output_info("uploading file: " + std::string(p->upload_from_path));
    for (memset(buffer, 0, sizeof(buffer)), res = read_file_with_log(fd, buffer, sizeof(buffer), "read upload file"),
                 up_file_msg.clear(), up_file_msg.type = MSG_TYPE_UPLOAD, strcpy(up_file_msg.fname, p->upload_to_path);
         res > 0;
         memset(buffer, 0, sizeof(buffer)), res = read_file_with_log(fd, buffer, sizeof(buffer), "read upload file"),
                 up_file_msg.clear(), up_file_msg.type = MSG_TYPE_UPLOAD, strcpy(up_file_msg.fname,
                                                                                 p->upload_to_path)) {

        memcpy(up_file_msg.buffer, buffer, res);
        up_file_msg.bytes = (int) res;
        res = write_net_with_log(p->client_socket, &up_file_msg, sizeof(MSG), "upload file data");
        // 延时防止服务端错乱
        usleep(0.2 * 1000 * 1000);
        if (res < 0) {
            output_info("fail to upload");
            break;
        }
    }
    output_info("uploaded file: " + std::string(p->upload_from_path));

    free(p->upload_to_path);
    free(p->upload_from_path);
    free(p);
    return nullptr;
}

// 上传函数
void func_upload(int client_socket, pthread_t &pthread_id) {
    std::string input;
    char *upload_to_path, *upload_from_path;

    // 注意: 需要占用的线程参数申请新的空间，避免多线程同时访问(不能设为全局变量)

    // 服务端目标路径
    input = input_with_hint("please input the file_path(relative path) where upload to");
    upload_to_path = (char *) malloc(sizeof(char) * ((QUERY_PATH + input).length() + 1));
    strcpy(upload_to_path, (QUERY_PATH + input).c_str());
    output_info("upload to file_path: " + std::string(upload_to_path));

    // 客户端文件路径
    input = input_with_hint("please input the file_path(relative path) where upload from");
    upload_from_path = (char *) malloc(sizeof(char) * ((UPLOAD_PATH + input).length() + 1));
    strcpy(upload_from_path, (UPLOAD_PATH + input).c_str());
    output_info("upload from file_path: " + std::string(upload_from_path));

    // 新建线程上传，避免阻塞
    pthread_create(&pthread_id, nullptr, thread_upload,
                   &(*((param_upload *) malloc(sizeof(param_upload))) =
                             param_upload{client_socket, upload_to_path, upload_from_path}));

}

int main() {
    printf("[Hello] I'm client!\n");

    // 初始化 client_socket
    int client_socket = init_client_socket();
    if (client_socket < 0) {
        printf("[Goodbye]\n");
        return 0;
    }

    // 创建接收专用线程
    pthread_t pthread_id;
    pthread_create(&pthread_id, nullptr, thread_receive, &client_socket);

    // UI
    net_disk_ui();

    // 循环发送
    MSG send_msg = {0};
    std::string command;
    for (std::cin >> command; !std::cin.eof(); std::cin >> command, send_msg.clear()) {

        if (command == "\n") {
            continue;
        }
        if (command.length() != 1) {
            output_error("unknown command: " + command);
            continue;
        }

        switch (command[0]) {
            case '1':
                func_query(client_socket, send_msg);
                break;
            case '2':
                func_download(client_socket, send_msg);
                break;
            case '3':
                func_upload(client_socket, pthread_id);
                break;
            case '4':
                system("clear");
                net_disk_ui();
                break;
            case '5':
                printf("[Goodbye]\n");
                return 0;
            default:
                output_error("unknown command: " + command);
        }
    }
    printf("[Goodbye]\n");
    return 0;
}
