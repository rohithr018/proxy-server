#include<iostream>
#include<netdb.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<cstring>

using namespace std;

constexpr int BUFFER_SIZE = 4096;

int main(){

    // creating socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd<0){
        perror("socket failed");
        return -1;
    }

    // binding socket to port
    sockaddr_in addr{};;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;  //accept from any ip
    addr.sin_port = htons(8080);  //port 8080

    if(bind(server_fd, (sockaddr*)&addr, sizeof(addr))<0){
        perror("Bind failed");
        close(server_fd);
        return -1;
    }

    // listening for connections
    if(listen(server_fd, 5)<0){
        perror("Listen failed");
        close(server_fd);
        return -1;
    }

    cout<<"Proxy Server is listening on port 8080..."<<endl;

    // accepting one connection
    int client_fd = accept(server_fd, nullptr, nullptr);
    if(client_fd < 0){
        perror("Accept failed");
        close(server_fd);
        return -1;
    }

    cout<<"Client connected!"<<endl;

    //Read request
    char buffer[BUFFER_SIZE];
    ssize_t bytes= recv(client_fd,buffer,BUFFER_SIZE,0);

    if(bytes <= 0){
        perror("recv failed");
        close(client_fd);
        close(server_fd);
        return -1;
    }

    string request(buffer, bytes);

    cout<<"Received "<<bytes<<" bytes from client."<<endl;
    cout<<"Received received:\n "<<request<<"\n";

    //extract host header
    size_t host_pos = request.find("Host: ");
    if(host_pos == string::npos){
        cerr<<"Host header not found in request."<<endl;
        close(client_fd);
        close(server_fd);
        return 1;
    }

    size_t host_end = request.find("\r\n", host_pos);
    string host = request.substr(host_pos + 6, host_end - (host_pos + 6));

    cout<<"Extracted Host: "<<host<<endl;


    //Resolve and connect to origin server

    addrinfo hints{}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if(getaddrinfo(host.c_str(), "80", &hints, &res) != 0){
        perror("getaddrinfo failed");
        return 1;
    }

    int origin_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(connect(origin_fd,res->ai_addr,res->ai_addrlen) < 0){
        perror("connect to origin server failed");
        freeaddrinfo(res);
        return 1;
    }

    freeaddrinfo(res);

    //Forward request to origin server
    send(origin_fd, request.c_str(), request.size(), 0);

    while((bytes=recv(origin_fd, buffer, BUFFER_SIZE, 0)) > 0){
        send(client_fd, buffer, bytes, 0);
    }
       
    //cleanups
    close(origin_fd);
    close(client_fd);
    close(server_fd);

    cout<<"Request completed."<<endl;
    return 0;
    
}