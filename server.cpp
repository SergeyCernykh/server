#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <crypt.h>
#include <vector>
#include "csignal"
#include "json.hpp"

using namespace std;
using json = nlohmann::json;

#define good_ans "HTTP-1.0 200 OK\r\n"
#define bad_ans "HTTP-1.0 409 Bad request\r\n\r\n\0"
#define good_req "POST /hash HTTP/1.0\r\n"
#define close_ans "HTTP-1.0 200 Closing...\r\n"

struct options {
    char* ip;
    int port;
    int bl;
    int type_of_multiplexing;
};

class Server {
    int backlog;
    struct sockaddr_in serv_addr;
    char buf[1024];

public:
    static int listen_fd;
    Server(int pn, int bl, const char* addr);
    void start_server ();
    static void close_server (int sign);
    void work_with_client(int fd);
    virtual void multiplexing()=0;
    int get_listen_fd() {return listen_fd;}
    char* pars_request(char* str, int fd);
    char* sha512(char* JSON);
    json json_check(char* text_req);
    void bad_answer(int fd);
    json SHA512(json j);
};
int Server ::listen_fd = 0;

class Server_select : public Server {
    fd_set set_of_sockets;
    int max_d;
    vector<int> clients_fd;
public:
    Server_select(int pn, int bl, const char*addr):Server(pn,bl,addr){FD_ZERO(&set_of_sockets);}
    void multiplexing();
};

options options_parser (int argc, char** argv) {
    int i;
    options op;

    op.port=8080;
    op.ip = new char[sizeof("0.0.0.0")+1];
    strcpy(op.ip,"0.0.0.0");
    op.bl=5;
    op.type_of_multiplexing=1;
    
    while ((i=getopt(argc, argv, "l:p:m:b:")) > 0) {
        if ((char)i=='l') {
            op.ip = new char[strlen(optarg)+1];
            strcpy(op.ip,optarg);
        }
        if ((char)i=='p') {
            op.port=atoi(optarg);
        }
        if ((char)i=='m') {
            if (!strcmp("select",optarg)) {
                op.type_of_multiplexing=1;
            }
        }
        if ((char)i=='b') {
            op.bl=atoi(optarg);
        }
    }
    return op;
}


int main (int argc, char** argv){
    options op = options_parser(argc, argv);
    Server_select serv(op.port, op.bl, op.ip);
    serv.start_server();
    serv.multiplexing();
    return 0;
}


void Server::start_server (){
    //создаем сокет
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0))<0) {
        cout << "Can't creat socket!" << endl;
        exit(0);
    }

    //связываем сокет
    if (bind(listen_fd, (struct sockaddr*) &serv_addr, sizeof(serv_addr))<0) {
        cout << "Can't bind socket!" << endl;
        exit(0);
    }

    cout << "Информация о сервере: \n ip: ";
    printf("%s", inet_ntoa((in_addr)serv_addr.sin_addr));
    cout << endl;
    cout << " Порт: " << ntohs(serv_addr.sin_port) << endl;
    cout << " Размер очереди: " << backlog << endl;
    //начинаем обработку запросов на соединение
    if (listen(listen_fd, backlog) < 0) {
        cout << "Can't listen socket!" << endl;
        close(listen_fd);
        exit(0);
    }

    cout << "Server is ready!" << endl;
    return;
}

void Server :: close_server (int sign) {
    shutdown(Server :: listen_fd, 2);
    close(Server :: listen_fd);
    cout << "Server was closed!" << endl;
    exit(0);
}

Server :: Server(int pn, int bl, const char* addr){
    backlog = bl;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(addr);
    serv_addr.sin_port = htons(pn);
}

void Server:: work_with_client(int fd) {
    if(!fork()) {
        cout<<"\n\n\n\nNew client!\n";
        close(listen_fd);
        int len = read(fd, buf, 1024);
        if (len < 0) {
            cout << "Error in read!" << endl;
            shutdown(fd, 2);
            close(fd);
        }
        buf[len]='\0';

        char* req = new char[len+1];
        strcpy(req, buf);
        req[len]='\0';
        string str;
        if ((req = pars_request(req,fd)) != NULL) {
            json j = json_check(req);
            if (!j.empty()) {
                j=SHA512(j);
                str = j.dump();
            } else { bad_answer(fd);}
        } else { bad_answer(fd); } 

        char * res = new char[strlen(good_ans)+str.size()+7];
        strcpy(res, good_ans);
        strcat(res,"\r\n");
        strcat(res, str.c_str());
        strcat(res,"\r\n\0");
        write(fd, res, strlen(res));
        cout << getpid() <<" OK!" << endl;
        shutdown(fd, 2);
        close(fd);
        exit(0);
    }
    return;
}

void Server_select::multiplexing() {
    signal(SIGUSR1, close_server);
    for(;;) {
        int fd, l_fd;
        max_d=l_fd=get_listen_fd();
        FD_SET(l_fd,&set_of_sockets);
        
        for (int i=0; i < clients_fd.size(); i++){
            FD_SET((fd=clients_fd[i]), &set_of_sockets);
            if (fd>max_d) max_d=fd;
        }
        
        if (select(max_d+1,&set_of_sockets,NULL,NULL,NULL)<1) {
            cout << "Error in select!" << endl;
        }
        
        if(FD_ISSET(l_fd,&set_of_sockets)) {
            int new_fd;
            if ((new_fd = accept(l_fd, NULL, NULL))<0) {
                cout<<"Error accepting connection!"<< endl;
                exit(0);
            }
            clients_fd.insert(clients_fd.end(),new_fd);
            FD_CLR(l_fd, &set_of_sockets);
        }
    
        for (int i=0; i < clients_fd.size(); i++){
            fd = clients_fd[i];
            if (FD_ISSET(fd,&set_of_sockets)) {
                //обработка запроса от i-го клиента
                work_with_client(fd);
                close(fd);
                clients_fd.erase(clients_fd.begin()+i-1);
                FD_CLR(fd,&set_of_sockets);
            }
        }
    }
}

char* Server:: pars_request(char* req, int fd) {
    cout << req << endl;
    if ((req[0] == 'G') && (req[1] == 'E') && (req[2] == 'T')) {
        kill(getppid(),SIGUSR1);
        write(fd,close_ans,strlen(close_ans));
        shutdown(fd, 2);
        close(fd);
        exit(0);
    }
    char* header = new char[strlen(good_req)+1];
    strncpy(header, req, strlen(good_req));
    cout <<header<< endl; 
    if (strcmp(header, good_req) != 0) {cout << "Header is bad"<< endl; return NULL;}
    for(req = req+strlen(good_req); (*req !='{') && (*req != '\0'); req++) {}
    if (*req == '\0') {cout << "Request is bad"<< endl; return NULL;}
    return req;
}

json Server :: json_check(char* text_req){
    json j,j_e;
    try {
        j = json :: parse(text_req);
        if ((j.count("rounds") == 1) && (j.count("str") == 1) && (j.size() == 2)) {
            if ((j["rounds"].is_number_unsigned()) && (j["str"].is_string())) {
                return j;
            } else {cout << "JSON's parametrs are bad!" << endl; return j_e;}
        } else {cout << "JSON is bad!"<< endl; return j_e;}
    } catch (json::parse_error& e) {cout << "JSON is bad:\n"<<e.what() << endl; return j_e;}
}

void Server :: bad_answer(int fd) {
     write(fd,bad_ans,strlen(bad_ans));
     shutdown(fd, 2);
     close(fd);
     exit(0);
}

json Server :: SHA512(json j) {
    string str = j["str"];
    const char* cs = str.c_str();
    char* result = crypt(cs,"$6$");
    for (int i = 1; i<j["rounds"]; i++){
       result = crypt(result,"$6$");
    }
    j.push_back(json::object_t::value_type("sha512", result));
    return j;
}
