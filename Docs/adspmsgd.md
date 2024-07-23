# Low-Level Design Document for ADSPMSGD

## 1. Introduction
This document provides a detailed low-level design for the adspmsgd module. It outlines the implementation details, data structures, algorithms, and interactions with other components.

## 2. Module Overview
**Module Name**: adspmsgd  
**Description**: The adspmsgd framework is a crucial component that bridges the communication between the DSP skel libraries and the application’s logging framework (for example logcat/printf). It essentially enables the skel libraries to log their messages directly into the application’s log framework. This is beneficial for developers because they typically focus on the application logs when debugging, making it easier for them to track and resolve issues. This mechanism enhances the efficiency of the debugging process.  
**Responsibilities**:
- Establishes a shared buffer between the DSP and APSS.
- Enables the DSP to inscribe messages into this buffer.
- Permits the APPS to extract these messages from the buffer and relay them to the application’s logging framework.

## 3. Data Structures

```c
typedef struct {
  volatile int threadStop; // variable to stop the adspmsgd reader thread
  unsigned int bufferSize; // size of shared buffer
  unsigned int readIndex; // the index from which reader thread starts reading
  unsigned int* currentIndex; // if currentIndex is same as readIndex then msgd thread waits for messages from DSP
  char* headPtr; // head pointer to the msgd shared buffer
  char* message; // scratch buffer used to printing messages in application logging framework
  pthread_t msgreader_thread; // thread data structure
  FILE *log_file_fd; // file descriptor to save runtime farf logs, if set
} msgd;
```

## 4. Msg/Packet format
In ADSPMSGD, the shared buffer contains multiple string messages from DSP. Each message is stored in the buffer and is ended with a null character to indicate its conclusion. This format allows the system to efficiently store, read, and manage multiple messages within a single shared buffer.

![Design](Docs/images/adspmsgd_msg_format.png)

currentIndex: 21

In this example, the shared buffer contains three message: “Hello”, “World”, and “ADSPMSGD”. Each message is terminated by a null character (\0). The currentIndex is now pointing to the index after the last null character in the buffer. This is typically the position where a new message would start to be stored.

## 5. Design Diagram

![Design](Docs/images/adspmsgd.png)

## 6. Function Definitions
**Initialization Function**:
```c
int adspmsgd_init(remote_handle64 handle, int filter);
```

**Message Handling Function**:
```c
void readMessage(int domain);
```
**Logger Thread**:
```c
static void *adspmsgd_reader(void *arg)
```

## 7. Interface Definitions

Although this module doesn’t define any public interfaces, clients can still access its features. All they need to do is create a .FARF file in the application’s running directory and input the appropriate runtime FARF mask values, as specified in the Hexagon SDK.

