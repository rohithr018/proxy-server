#include<iostream>
#include<netdb.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<cstring>
#include<thread>
#include<queue>
#include<mutex>
#include<condition_variable>
#include<vector>
#include<errno.h>

using namespace std;

constexpr int BUFFER_SIZE = 4096;

const char* port_env = std::getenv("PORT");
int PORT = port_env ? std::stoi(port_env) : 8080;

constexpr int WORKER_COUNT = 4;

queue<int> task_queue;
mutex queue_mutex;
condition_variable queue_cv;

void send_http_error(int client_fd, int code, const string& message){
    string body = message+"\n";
    string response = "HTTP/1.1 " + to_string(code) + " " + message + "\r\n" +
                      "Content-Type: text/plain\r\n" +
                      "Content-Length: " + to_string(body.size()) + "\r\n" +
                      "Connection: close\r\n"
                      "\r\n" +
                      body;

    send(client_fd, response.c_str(), response.size(), 0);
}

void set_socket_timeout(int fd, int seconds){
    timeval timeout{};
    timeout.tv_sec = seconds;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

void worker_thread(){
    while(true){
        int client_fd;

        // wait for work(until queue is not empty) and get client fd from queue
        {
            unique_lock<mutex> lock(queue_mutex);
            queue_cv.wait(lock, []{ return !task_queue.empty(); });
            client_fd = task_queue.front();
            set_socket_timeout(client_fd, 5); //set 5 second timeout for client socket
            task_queue.pop();
        }

        // handle client
        //Receive request from client

        char buffer[BUFFER_SIZE];

        cout<<"Handling client in thread [THREAD_ID]:["<<this_thread::get_id()<<"]"<<endl;

        string request;
        char buf[BUFFER_SIZE];

        bool client_error=false;
        while(request.find("\r\n\r\n") == string::npos){ //until end of headers
            ssize_t bytes = recv(client_fd, buf, BUFFER_SIZE, 0);
            if(bytes <= 0){
                send_http_error(client_fd, 400, "Failed to receive data from client.");
                if(errno == EWOULDBLOCK || errno == EAGAIN){
                    send_http_error(client_fd, 408, "Client read timed out.");
                }
                client_error=true;
                break ;
            }
            request.append(buf, bytes);
        }

        if(client_error){
            close(client_fd);
            continue ;
        }
        
        cout<<"Request received: \n"<<request<<endl;

        size_t line_end = request.find("\r\n");
        string request_line = request.substr(0, line_end);
        
        size_t method_end = request_line.find(' ');
        size_t url_end = request_line.find(' ', method_end + 1);

        string method= request_line.substr(0, method_end);
        string url = request_line.substr(method_end + 1, url_end - method_end -
        1);
        string version = request_line.substr(url_end + 1);


        string path=url;

        if(url.find("http://") == 0){
            size_t path_pos = url.find('/', 7); // skip http://
            path=(path_pos != string::npos) ? url.substr(path_pos) : "/";
            
        }

        string new_request = method + " " + path + " " + version;
        request.replace(0, line_end, new_request); //replace request line with modified one
        
        //extract host header
        size_t host_pos = request.find("Host: ");
        if(host_pos == string::npos){
            send_http_error(client_fd, 400, "Host header not found in request.");
            close(client_fd);
            continue ;
        }

        //Extract host value
        size_t host_end = request.find("\r\n", host_pos);

        string host_port = request.substr(host_pos + 6, host_end - (host_pos + 6));
        string host=host_port;
        string port="80";

        size_t colon_pos = host_port.find(':');
        if(colon_pos != string::npos){
            host = host_port.substr(0, colon_pos);
            port = host_port.substr(colon_pos + 1);
        }
        cout<<"Extracted Host: "<<host<<endl;


        //Resolve host
        addrinfo hints{}, *res;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        if(getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0){
            send_http_error(client_fd, 502, "Bad Gateway");
            close(client_fd);
            continue ;
        }


        //Create socket and connect to origin server
        int origin_fd = socket(
            res->ai_family, 
            res->ai_socktype, 
            res->ai_protocol
        );
        set_socket_timeout(origin_fd, 5); //set 5 second timeout for origin socket
        if(connect(origin_fd,res->ai_addr,res->ai_addrlen) < 0){
            send_http_error(client_fd, 502, "Failed to connect to origin server: "+host);
            freeaddrinfo(res);
            close(client_fd);
            continue ;
        }

        freeaddrinfo(res);

        //Remove proxy header is present 
        size_t proxy_pos = request.find("Proxy-Connection: ");
        if(proxy_pos != string::npos){
            size_t proxy_end = request.find("\r\n", proxy_pos);
            request.erase(proxy_pos, proxy_end - proxy_pos + 2); //remove line
        }

        //Force connection close to simplify proxy logic
        size_t connection_pos = request.find("Connection: ");
        if(connection_pos == string::npos){
            size_t header_end = request.find("\r\n\r\n");
            request.insert(header_end, "\r\nConnection: close");
        }

        
        //Forward request to origin server
        send(origin_fd, request.c_str(), request.size(), 0);
        
        cout << "Forwarded request \n" << request <<endl;
        
        //Strean response back to client

        while(true){
            ssize_t bytes = recv(origin_fd, buffer, BUFFER_SIZE, 0);
            if(bytes <= 0){
                if(errno == EWOULDBLOCK || errno == EAGAIN){
                    send_http_error(client_fd, 504, "Origin server read timed out.");
                }
                break; //end of response or error
            }
            send(client_fd, buffer, bytes, 0);
        }


        //Cleanup
        close(origin_fd);
        close(client_fd);


    }
}


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
    addr.sin_port = htons(8080);        //port 8080

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 50);

    cout<<"Proxy Server is listening on the port:"<< PORT <<endl;


    //Start worker threads
    vector<thread> workers;
    for(int i=0; i<WORKER_COUNT; ++i){
        workers.emplace_back(worker_thread);
    }


    //Accept loop
    while(true){
        int client_fd = accept(server_fd, nullptr, nullptr);
        if(client_fd < 0){
            perror("accept failed");
            continue;
        }
        {
            //Add client fd to task queue
            {
                lock_guard<mutex> lock(queue_mutex);
                task_queue.push(client_fd);
            }
            queue_cv.notify_one();
        }

    }
    close(server_fd);
    return 0;
    
}