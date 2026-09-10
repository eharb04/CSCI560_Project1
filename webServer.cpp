// **************************************************************************************
// * webServer (webServer.cpp)
// * - Implements a very limited subset of HTTP/1.0, use -v to enable verbose debugging output.
// * - Port number 1701 is the default, if in use random number is selected.
// *
// * - GET requests are processed, all other metods result in 400.
// *     All header gracefully ignored
// *     Files will only be served from cwd and must have format file\d.html or image\d.jpg
// *
// * - Response to a valid get for a legal filename
// *     status line (i.e., response method)
// *     Cotent-Length:
// *     Content-Type:
// *     \r\n
// *     requested file.
// *
// * - Response to a GET that contains a filename that does not exist or is not allowed
// *     statu line w/code 404 (not found)
// *
// * - CSCI 471 - All other requests return 400
// * - CSCI 598 - HEAD and POST must also be processed.
// *
// * - Program is terminated with SIGINT (ctrl-C)
// **************************************************************************************
#include "webServer.h"


// **************************************************************************************
// * Signal Handler.
// * - Display the signal and exit (returning 0 to OS indicating normal shutdown)
// * - Optional for 471, required for 598
// **************************************************************************************
void sig_handler(int signo) {
  DEBUG << "Caught signal #" << signo << ENDL;
  DEBUG << "Closing file descriptors 3-31." << ENDL;
  closefrom(3);
  exit(0);
}


// **************************************************************************************
// * processRequest,
//   - Return HTTP code to be sent back
//   - Set filename if appropriate. Filename syntax is validated but existance is not verified.
// **************************************************************************************
int readHeader(int sockFd, std::string &fileName, int &method, std::string &header) {
  int returnCode = 400; // Default
  int bytesRead;
  header = ""; // Clear header

  while(header.find("\r\n\r\n") == std::string::npos) {
    char buffer[BUFFER_SIZE];
    bytesRead = read(sockFd, buffer, BUFFER_SIZE);

    if (bytesRead <= 0) {
      DEBUG << "read() returned " << bytesRead << ".  Closing connection." << ENDL;
      return 0;
    }

    for (int i = 0; i < bytesRead; i++) {
      header += buffer[i];
    }
  }

  size_t endOfFirstLine = header.find("\r\n");
  std::string firstLine = header.substr(0, endOfFirstLine);
  size_t firstSpace = firstLine.find(" ");
  std::string methodString = firstLine.substr(0, firstSpace);
  size_t secondSpace = firstLine.find(" ", firstSpace + 1);
  fileName = firstLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);

  if (methodString == "GET") {
    method = GET;
  } else if (methodString == "HEAD") {
    method = HEAD;
  } else if (methodString == "POST") {
    method = POST;
  } else {
    return returnCode; // Invalid method
  }

  bool validFileName = std::regex_match(fileName, std::regex("/file[0-9]\\.html")) || 
                       std::regex_match(fileName, std::regex("/image[0-9]\\.jpg"));
  if (validFileName) {
    returnCode = 200; // Valid method and file
  } else {
    returnCode = 404; // File not found
  }

  return returnCode;
}


// **************************************************************************
// * Send one line (including the line terminator <LF><CR>)
// * - Assumes the terminator is not included, so it is appended.
// **************************************************************************
void sendLine(int sockFd, const std::string &stringToSend) {
  char fullLine[stringToSend.length() + 2]; // +2 for \r\n
  std::copy(stringToSend.begin(), stringToSend.end(), fullLine);
  fullLine[stringToSend.length()] = '\r';
  fullLine[stringToSend.length() + 1] = '\n';
  
  write(sockFd, fullLine, sizeof(fullLine)); // Send array
}

// **************************************************************************
// * Send the entire 404 response, header and body.
// **************************************************************************
void send404(int sockFd) {
  sendLine(sockFd, "HTTP/1.0 404 Not Found");
  sendLine(sockFd, "content-type: text/html");
  sendLine(sockFd, "");
  sendLine(sockFd, "File not found");
  sendLine(sockFd, "");
}

// **************************************************************************
// * Send the entire 400 response, header and body.
// **************************************************************************
void send400(int sockFd) {
  sendLine(sockFd, "HTTP/1.0 400 Bad Request");
  sendLine(sockFd, "");
}

void send200(int sockFd) {
  sendLine(sockFd, "HTTP/1.0 200 OK");
}

void send201(int sockFd) {
  sendLine(sockFd, "HTTP/1.0 201 Created");
}

// POST method
// Save file to disk
void post(int sockFd, std::string &fileName, std::string &header) {
  // Handle error codes
  // 200 if overwrite
  // 201 if new file created
  struct stat fileStat;
  // fileName = "./data" + fileName;
  if (stat(fileName.c_str(), &fileStat) == 0) {
    send200(sockFd);
  } else {
    send201(sockFd);
  }

  // Get length of content from header
  size_t contentLength;
  size_t startPos = header.find("Content-Length: ") + 16; // Length of Content-length 
  size_t endPos = header.find("\r\n", startPos);
  std::string contentLengthStr = header.substr(startPos, endPos - startPos);
  contentLength = std::stoul(contentLengthStr);

  // Get body
  std::string body = header.substr(header.find("\r\n\r\n") + 4); // +4 to skip to body
  char buffer[BUFFER_SIZE];
  while (body.length() < contentLength) {
    memset(buffer, 0, BUFFER_SIZE); // Zero out buffer
    size_t bytesToRead = std::min((size_t)BUFFER_SIZE, contentLength - body.length()); // Don't read more than needed
    size_t bytesRead = read(sockFd, buffer, bytesToRead);
    if (bytesRead <= 0) {
      DEBUG << "read() returned " << bytesRead << ".  Closing connection." << ENDL;
      return;
    }
    body.append(buffer, bytesRead); // Add to body
  }

  // Write body to file
  std::ofstream f(fileName, std::ios::binary);
  f.write(body.c_str(), body.length());
  f.close();
}

// Send just the header
int sendHead(int sockFd, std::string fileName) {
  struct stat fileStat;
  if (stat(fileName.c_str(), &fileStat) != 0) { // Read denied
    send404(sockFd);
    return -1;
  }
  size_t fileSize = fileStat.st_size;

  // Good request
  send200(sockFd);

  std::string contentType;
  if (fileName.find(".html") != std::string::npos) {
    contentType = "text/html";
  } else if (fileName.find(".jpg") != std::string::npos) {
    contentType = "image/jpeg";
  } else { // Avoid unknown behavior
    send400(sockFd);
    return -1;
  }
  sendLine(sockFd, "content-type: " + contentType);
  sendLine(sockFd, "content-length: " + std::to_string(fileSize));
  sendLine(sockFd, ""); // Blank line to indicate end of header

  return fileSize; // Return for sendFile to use;
}


// **************************************************************************************
// * sendFile
// * -- Send a file back to the browser.
// **************************************************************************************
void sendFile(int sockFd, std::string fileName) {
  int fileSize = sendHead(sockFd, fileName);
  if (fileSize == -1) { // Failed to send header
    return;
  }

  // Send file
  std::ifstream f(fileName, std::ios::binary);
  char buffer[BUFFER_SIZE];
  size_t bytesRead = 0;
  size_t smallBytesRead;
  while (bytesRead < fileSize) {
    memset(buffer, 0, BUFFER_SIZE); // Zero out buffer
    f.read(buffer, BUFFER_SIZE);
    smallBytesRead = f.gcount(); // Number of bytes read
    write(sockFd, buffer, smallBytesRead);

    bytesRead += smallBytesRead; // Update total bytes read
  }
}


// **************************************************************************************
// * processConnection
// * -- process one connection/request.
// * -- sockFd: fd for socket that is connected to client
// **************************************************************************************
int processConnection(int sockFd) {
  // bool close = false;
  // bool lineTerminator = false;
  // int bytesRead;
  // std::string container;

  // while(!close) { // Loop until "CLOSE" is received
  //   container = ""; // Will hold whole line
  //   char buffer[BUFFER_SIZE];
  //   lineTerminator = false;

  //   while (!lineTerminator) { // Loop until '\n' is received
  //     bytesRead = read(sockFd, buffer, BUFFER_SIZE);

  //     if (bytesRead <= 0) {
  //       DEBUG << "read() returned " << bytesRead << ".  Closing connection." << ENDL;
  //       return 0;
  //     }

  //     for (int i = 0; i < bytesRead; i++) {
  //       container += buffer[i];
  //       if (buffer[i] == '\n') {
  //         lineTerminator = true;
  //       }
  //     }
  //   }

  //   write(sockFd, container.c_str(), container.length()); // Echo the line back to the client

  //   // "CLOSE" was found
  //   if (container.find("CLOSE") != std::string::npos) {
  //     close = true;
  //   }
  // }
 
  // Call readHeader()
  std::string fileName, header;
  int method;
  int returnCode = readHeader(sockFd, fileName, method, header);
  fileName = "./data" + fileName; // Prepend data directory to filename

  // If read header returned 400, send 400
  if (returnCode == 400) {
    send400(sockFd);
    return 0;
  }

  // If read header returned 404, call send404
  if (returnCode == 404) {
    send404(sockFd);
    return(0);
  }

  // 471: If read header returned 200, call sendFile
  // if (returnCode == 200) {
  //   sendFile(sockFd, fileName);
  // }
  
  // 598 students
  // - If the header was valid and the method was GET, call sendFile()
  // - If the header was valid and the method was HEAD, call a function to send back the header.
  // - If the header was valid and the method was POST, call a function to save the file to dis.
  if (method == GET) { // GET
    sendFile(sockFd, fileName);
  } else if (method == HEAD) { // HEAD
    sendHead(sockFd, fileName);
  } else if (method == POST) { // POST
    post(sockFd, fileName, header);
  }

  return 0;
}
    

int main (int argc, char *argv[]) {


  // ********************************************************************
  // * Process the command line arguments
  // ********************************************************************
  int opt = 0;
  while ((opt = getopt(argc,argv,"d:")) != -1) {
    
    switch (opt) {
    case 'd':
      LOG_LEVEL = std::stoi(optarg);
      break;
    case ':':
    case '?':
    default:
      std::cout << "useage: " << argv[0] << " -d LOG_LEVEL" << std::endl;
      exit(-1);
    }
  }


  // *******************************************************************
  // * Catch all possible signals
  // ********************************************************************
  DEBUG << "Setting up signal handlers" << ENDL;
  signal(SIGINT, sig_handler);
  

  
  // *******************************************************************
  // * Creating the inital socket using the socket() call.
  // ********************************************************************
  int listenFd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenFd < 0) {
    ERROR << "Failed to create socket" << ENDL;
    exit(1);
  }
  DEBUG << "Calling Socket() assigned file descriptor " << listenFd << ENDL;

  // Allow rebinding after close so it doesn't wait for port to be released
  int reuseAddr = 1;
  if (setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr)) < 0) {
    ERROR << "Failed to set SO_REUSEADDR" << ENDL;
    exit(1);
  }

  
  // ********************************************************************
  // * The bind() call takes a structure used to spefiy the details of the connection. 
  // *
  // * struct sockaddr_in servaddr;
  // *
  // On a cient it contains the address of the server to connect to. 
  // On the server it specifies which IP address and port to lisen for connections.
  // If you want to listen for connections on any IP address you use the
  // address INADDR_ANY
  // ********************************************************************
  struct sockaddr_in servaddr{};
  servaddr.sin_family = PF_INET; // IPv4
  servaddr.sin_addr.s_addr = INADDR_ANY; // Listen on any IP address
  // servaddr.sin_port = htons(6767); // Port number > 1028, convert to network byte order


  // ********************************************************************
  // * Binding configures the socket with the parameters we have
  // * specified in the servaddr structure.  This step is implicit in
  // * the connect() call, but must be explicitly listed for servers.
  // *
  // * Don't forget to check to see if bind() fails because the port
  // * you picked is in use, and if the port is in use, pick a different one.
  // ********************************************************************
  uint16_t port = 6767;
  bool bound = false;
  while (!bound) {
    servaddr.sin_port = htons(port);
    DEBUG << "Calling bind() on port " << port << ENDL;
    if (bind(listenFd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == 0) {
      bound = true;
    } else if (errno == EADDRINUSE) { // Port number is being used
      DEBUG << "Port " << port << " is in use, trying the next one." << ENDL;
      port++; // Try next port number
    } else { // Check for binding error
      ERROR << "Failed to bind socket: " << strerror(errno) << ENDL;
      exit(1);
    }
  }
  std::cout << "Using port: " << port << std::endl;


  // ********************************************************************
  // * Setting the socket to the listening state is the second step
  // * needed to being accepting connections.  This creates a que for
  // * connections and starts the kernel listening for connections.
  // ********************************************************************
  DEBUG << "Calling listen()" << ENDL;
  if (listen(listenFd, 10) < 0) { // Max of 10 connections
    ERROR << "Failed to listen on socket" << ENDL;
    exit(1);
  }


  // ********************************************************************
  // * The accept call will sleep, waiting for a connection.  When 
  // * a connection request comes in the accept() call creates a NEW
  // * socket with a new fd that will be used for the communication.
  // ********************************************************************
  int quitProgram = 0;
  while (!quitProgram) {
    int connFd = 0;
    DEBUG << "Calling connFd = accept(listenFd, NULL, NULL)." << ENDL;
    connFd = accept(listenFd, NULL, NULL);
    

    DEBUG << "We have recieved a connection on " << connFd << ". Calling processConnection(" << connFd << ")" << ENDL;
    quitProgram = processConnection(connFd);
    DEBUG << "processConnection returned " << quitProgram << " (should always be 0)" << ENDL;
    DEBUG << "Closing file descriptor " << connFd << ENDL;
    close(connFd);
  }  

  ERROR << "Program fell through to the end of main. A listening socket may have closed unexpectadly." << ENDL;
  closefrom(3);

}
