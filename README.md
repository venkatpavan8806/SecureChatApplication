# 🗨️ ChatApp

**ChatApp** is a simple client–server chat application written in **C++**, demonstrating how real-time communication works between multiple clients over a network.  
It uses **socket programming**, **multithreading**, and a **GUI-based client interface** for interactive chatting.

---

## 📦 Project Structure
```
ChatApp/
├── server.cpp        # Server-side source code
├── gui_client.cpp    # GUI-based client source code
```

---

## ⚙️ Features
- Real-time chat between multiple clients  
- Server handles multiple users concurrently  
- Lightweight C++ implementation  
- GUI client built with native C++ (no external frameworks required)  
- Easy setup and cross-platform support (Windows/Linux)

---

## 🧰 Requirements
Before building the project, make sure you have:
- **C++17 or later**  
- **g++** or **MinGW** compiler  
- (Optional) **SQLite3** headers and library, if you plan to store chat data  
- Basic knowledge of running programs via the terminal or command prompt  

---

## 🚀 Setup and Usage

### 🖥️ 1. Clone or Download
Download or clone this repository:
```bash
git clone https://github.com/yourusername/ChatApp.git
cd ChatApp
```

### ⚙️ 2. Compile the Server
Use the following command:
```bash
g++ server.cpp -o server
```

### 💬 3. Compile the GUI Client
```bash
g++ gui_client.cpp -o client
```

> 💡 If you’re using **SQLite**, link it during compilation:
```bash
g++ server.cpp sqlite3.c -o server
```

### ▶️ 4. Run the Server
Start the server first:
```bash
./server
```
By default, it listens on port **8080** (you can modify this in the code).

### 💻 5. Run One or More Clients
In separate terminals:
```bash
./client
```
Each client can now connect to the server and exchange messages in real time.

---

## 🗃️ Example: Chat Database Structure

If you’re using **SQLite**, here’s an example of how your `chat_server.db` might look:

### **Table: users**
| id | username   | password   | status  |
|----|-------------|------------|----------|
| 1  | Alice       | alice123   | online   |
| 2  | Bob         | bob456     | offline  |

### **Table: messages**
| id | sender_id | receiver_id | message               | timestamp           |
|----|------------|-------------|-----------------------|---------------------|
| 1  | 1          | 2           | Hello Bob!            | 2025-11-06 09:30:00 |
| 2  | 2          | 1           | Hi Alice!             | 2025-11-06 09:31:10 |
| 3  | 1          | 2           | How are you today?    | 2025-11-06 09:32:45 |

This shows how messages and user states are stored.  
You can create these tables manually in SQLite using:

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL,
    password TEXT NOT NULL,
    status TEXT
);

CREATE TABLE messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    sender_id INTEGER,
    receiver_id INTEGER,
    message TEXT,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(sender_id) REFERENCES users(id),
    FOREIGN KEY(receiver_id) REFERENCES users(id)
);
```

---

## 🔍 How It Works
- The **server** listens for incoming client connections on a fixed port.  
- Each new client is handled using **a separate thread**, allowing multiple users to chat simultaneously.  
- The **client** connects to the server using sockets and provides a simple GUI interface for message input/output.  
- Messages are broadcast to all connected clients through the server.

---

## 🧩 Customization
You can modify:
- Server port → inside `server.cpp`  
- GUI behavior → inside `gui_client.cpp`  
- Database schema → inside `chat_server.db` or the code itself  
- Add encryption, file transfers, or logging easily by extending this codebase.

---

## 🧑‍💻 Author
**Venkat Pavan**  
A C++ enthusiast exploring networking, socket programming, and GUI development.

---

## 🪄 License
This project is open-source and available under the **MIT License**.
