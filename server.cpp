#include <cstdio>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <cstring>
#include <pthread.h>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>

#define MSG_TYPE_QUERY    1      // 查询
#define MSG_TYPE_DOWNLOAD 2      // 下载
#define MSG_TYPE_UPLOAD   3      // 上传
#define MSG_TYPE_ERROR    4      // 错误

#define BUFFER_SIZE       1024   // buffer最大大小
#define NAME_SIZE         FILENAME_MAX     // 文件名(路径)最大大小

const char QUERY_PATH[] = "/home/draft/Clion/linux/server/"; // 查询路径(网盘根目录)
const char DOWNLOAD_PATH[] = "/home/draft/Clion/linux/server/"; // 下载路径(网盘发送文件的路径)
const char UPLOAD_PATH[] = "/home/draft/Clion/linux/server/"; // 上传路径(网盘保存文件的路径)

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
    output_debug("server <= " + m->toString() + " (" + std::to_string(res) + " bytes)" + " (" + hint + ")");
    return res;
}

// 从socket中读取(发送)信息，同时输出log
ssize_t write_net_with_log(int socket, MSG *m, size_t n, const std::string &hint) {
    ssize_t res = write(socket, m, n);
    output_debug("server => " + m->toString() + " (" + std::to_string(res) + " bytes)" + " (" + hint + ")");
    return res;
}

// 向socket中写入错误信息，同时输出log
ssize_t write_net_error_with_log(int accept_socket, const std::string &error, const std::string &hint) {
    MSG tmp = MSG{MSG_TYPE_ERROR, 0};
    strcpy(tmp.fname, error.c_str());
    return write_net_with_log(accept_socket, &tmp, sizeof(tmp), hint);
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

// 初始化server_socket
int init_server_socket() {
    int server_socket;
    // 初始化
    output_info("start creating server");
    {
        // 创建套接字
        if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("[Error] fail to create socket");
            return 0;
        }
        // 设置ip地址和端口
        struct sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(6667);
        // 设置复用ip端口号
        int opt_value = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt_value, sizeof(opt_value));
        // 绑定 ip地址和端口 到 套接字上
        if (bind(server_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
            output_error("fail to bind server");
            return 0;
        }
        // 监听端口
        if (listen(server_socket, 10) < 0) {
            output_error("fail to listen server");
            return 0;
        }
    }
    output_info("finish creating server");
    return server_socket;
}

// 查询函数
void func_query(int accept_socket, char *query_path_) {
    MSG info_msg = {.type = MSG_TYPE_QUERY};
    ssize_t res;
    std::string query_path = QUERY_PATH + std::string(query_path_);

    // 选择目录
    DIR *dp = opendir(query_path.c_str());
    if (nullptr == dp) {
        output_error("fail to open dir!");
        write_net_error_with_log(accept_socket, "dir no exist:" + query_path, "send error to client");
        return;
    }

    // 开始发送
    output_info("querying dir: " + query_path);

    // 循环发送目录
    struct dirent *dir;
    struct stat fileInfo{};
    for (dir = readdir(dp), info_msg.clear(); nullptr != dir; dir = readdir(dp), info_msg.clear()) {
        if (dir->d_name[0] == '.') continue;


        info_msg.type = MSG_TYPE_QUERY;
        strcpy(info_msg.fname, dir->d_name);

        // 判断文件类型：一般文件or文件夹or其他
        if (stat((query_path + dir->d_name).c_str(), &fileInfo) != 0) {
            output_error("Failed to get file info." + query_path + dir->d_name);
            write_net_error_with_log(accept_socket, "Failed to get file info." + query_path + dir->d_name,
                                     "send error to client");
            continue;
        }
        if (S_ISREG(fileInfo.st_mode)) {
            // 这是一个普通文件
        } else if (S_ISDIR(fileInfo.st_mode)) {
            // 这是一个目录
            info_msg.fname[strlen(info_msg.fname) + 1] = 0;
            info_msg.fname[strlen(info_msg.fname)] = '/';
        } else {
            // 其他类型，如符号链接等
        }

        // 发送
        res = write_net_with_log(accept_socket, &info_msg, sizeof(info_msg), "send path name");
        // 延时防止客户端错乱
        usleep(0.2 * 1000 * 1000);

        if (res < 0) {
            output_error("send menu error, unknown error!");
            return;
        }
    }
    // 发送完成
    output_info("queried dir: " + query_path);
}

// 下载函数
void func_download(int accept_socket, char *download_path_) {

    int fd;
    MSG file_msg = {0};
    std::string download_path = DOWNLOAD_PATH + std::string(download_path_);

    // 打开文件
    fd = open(download_path.c_str(), O_RDONLY);
    if (fd < 0) {
        output_error("fail to open file: " + download_path);
        write_net_error_with_log(accept_socket, "file no exist:" + download_path, "send error to client");
        return;
    }

    // 开始发送
    output_info("downloading file:" + download_path);

    // 循环发送
    ssize_t res;
    for (file_msg.clear(), strcpy(file_msg.fname, download_path_),
                 res = read_file_with_log(fd, file_msg.buffer, sizeof(file_msg.buffer), "read file data");
         res >= 0;
         file_msg.clear(), strcpy(file_msg.fname, download_path_),
                 res = read_file_with_log(fd, file_msg.buffer, sizeof(file_msg.buffer), "read file data")) {
        file_msg.type = MSG_TYPE_DOWNLOAD;
        file_msg.bytes = (int) res;
        if (res == 0) {
            break;
        }
        res = write_net_with_log(accept_socket, &file_msg, sizeof(MSG), "send file data");
        if (res < 0) {
            output_error("fail to send file data");
            break;
        }
    }
    // 发送完成
    output_info("downloaded file:" + download_path);
}

// 上传函数
void func_upload(int &fd, MSG &receive_msg) {
    ssize_t res;
    std::string upload_path = UPLOAD_PATH + std::string(receive_msg.fname);
    if (fd == -1) {
        fd = open(upload_path.c_str(), O_CREAT | O_WRONLY, 0666);
        // 开始接收(第一次接收)
        output_info("uploading file:" + upload_path);
    }
    res = write_file_with_log(fd, receive_msg.buffer, receive_msg.bytes, "collecting the upload data");
    if (res < 0) {
        output_info("fail to write file");
    }
    if (receive_msg.bytes < sizeof(receive_msg.buffer)) {
        output_info("collected all upload file");
        close(fd);
        fd = -1;
        // 完成接收(最后一次接收)
        output_info("uploaded file:" + upload_path);
    }

}

// 用于给每个客户端提供服务(监听)
void *thread_listen(void *arg) {

    static int fd = -1;
    char up_file_path[NAME_MAX] = {0};
    int accept_socket = *(int *) arg;
    ssize_t res;

    MSG receive_msg = {0};

    // 持续接收
    for (receive_msg.clear(); true; receive_msg.clear()) {
        // 接收
        res = read_net_with_log(accept_socket, &receive_msg, sizeof(MSG), "received, switching");

        if (res == 0) {
            output_info("connection close or lost");
            break;
        }
        // 判断类型
        switch (receive_msg.type) {
            case MSG_TYPE_QUERY: // 查询
                func_query(accept_socket, receive_msg.fname);
                break;
            case MSG_TYPE_DOWNLOAD: // 下载
                func_download(accept_socket, receive_msg.fname);
                break;
            case MSG_TYPE_UPLOAD: // 上传
                func_upload(fd, receive_msg);
                break;
            default:
                output_error(std::string("unknown type") + std::to_string(receive_msg.type));
        }
    }

    return nullptr;
}

int main() {
    printf("[Hello] I'm server!\n");

    // 初始化
    int server_socket = init_server_socket();
    int accept_socket;

    // 多线程
    output_info("start waiting client's connection");
    unsigned long count = 0;
    for (pthread_t pthread_id; true;) {
        // 接收客户端连接
        accept_socket = accept(server_socket, nullptr, nullptr);
        // 新建线程来处理连接
        pthread_create(&pthread_id, nullptr, thread_listen, &accept_socket);
        output_info(std::string("finish connecting NO.") + std::to_string(++count) +
                    " client (pthread_id=" + std::to_string(pthread_id) +
                    ", accept_socket=" + std::to_string(accept_socket) + ")");
        if (pthread_id < 0) {
            output_error("fail to create thread");
            break;
        }
    }

    printf("[Goodbye]\n");
    return 0;
}
