# P2P File Sharing System - Download Feature Integration Guide

## Overview
This guide explains how to integrate the complete download functionality into your existing P2P file sharing system.

## Architecture

### Client Side Components
1. **download.cpp** - Handles downloading files from peers
2. **peer_server.cpp** - Serves file pieces to other peers
3. **method.cpp** - Already exists (computes hashes for uploads)
4. **client.cpp** - Main client (updated to support downloads)

### Tracker Side Components
1. **upload.cpp** - Already exists (handles file metadata)
2. **command.cpp** - Updated with download command handlers
3. **tracker.cpp** - Already exists (main tracker logic)

## File Structure

```
project/
├── client/
│   ├── client.cpp          (updated)
│   ├── method.cpp          (existing)
│   ├── download.cpp        (new)
│   ├── peer_server.cpp     (new)
│   └── header.h            (updated)
│
├── tracker/
│   ├── tracker.cpp         (existing)
│   ├── command.cpp         (updated)
│   ├── upload.cpp          (existing)
│   ├── sync.cpp            (existing)
│   └── header.h            (updated)
│
├── Makefile
└── tracker_info.txt
```

## Installation Steps

### 1. Update Existing Files

**header.h** - Replace with the complete version provided

**client.cpp** - Replace with the updated version that includes:
- Peer server initialization
- Download command handling
- Shared file management

**command.cpp** - Add the new command handlers:
- `list files <group_id>`
- `download file <group_id> <filename>`
- `stop share <group_id> <filename>`

### 2. Add New Files

Create these new files in your project:
- `download.cpp` (client side)
- `peer_server.cpp` (client side)

### 3. Compile the Project

```bash
make clean
make all
```

This will create two executables:
- `tracker` - The tracker server
- `client` - The client application

## Usage Guide

### Starting the System

#### 1. Start Trackers

Terminal 1 (Tracker 1):
```bash
./tracker tracker_info.txt 1
```

Terminal 2 (Tracker 2):
```bash
./tracker tracker_info.txt 2
```

#### 2. Start Clients

Terminal 3 (Client 1):
```bash
./client 127.0.0.1:5000 tracker_info.txt 6001
```
- `127.0.0.1:5000` - Tracker address
- `tracker_info.txt` - Tracker list for failover
- `6001` - Port for peer-to-peer connections (THIS IS NEW!)

Terminal 4 (Client 2):
```bash
./client 127.0.0.1:5000 tracker_info.txt 6002
```

### File Operations

#### 1. Create Users and Login

Client 1:
```
>> create_user alice password123
>> login alice password123
```

Client 2:
```
>> create_user bob password456
>> login bob password456
```

#### 2. Create and Join Group

Client 1:
```
>> create_group group1
```

Client 2:
```
>> join_group group1
```

Client 1:
```
>> accept_request group1 bob
```

#### 3. Upload a File

Client 1:
```
>> upload file group1 /path/to/myfile.txt
```

The system will:
- Compute SHA1 hashes for the file and each piece
- Send metadata to the tracker
- Start serving the file on port 6001

#### 4. List Files in Group

Client 2:
```
>> list files group1
```

Output:
```
Files in group group1:
  myfile.txt (1048576 bytes, 1 seeder(s))
```

#### 5. Download a File

Client 2:
```
>> download file group1 myfile.txt /path/to/destination/myfile.txt
```

The system will:
- Request metadata from tracker
- Connect to seeders (Client 1)
- Download pieces and verify SHA1 hashes
- Assemble the complete file

#### 6. Check Download Progress

Client 2:
```
>> show downloads
```

Output:
```
[DOWNLOADING] myfile.txt - 45.50% complete
```

Or when complete:
```
[COMPLETED] myfile.txt - 100.00% complete
```

#### 7. Stop Sharing a File

Client 1:
```
>> stop share group1 myfile.txt
```

## How It Works

### Upload Process

1. **Client computes hashes** (method.cpp)
   - Reads file in 512KB pieces
   - Computes SHA1 for each piece and entire file

2. **Client sends metadata to tracker**
   - Format: `UPLOAD_META <uid> <group_id> <filename> <filesize> <file_sha1> <piece_count> <piece_hashes>`

3. **Tracker stores metadata** (upload.cpp)
   - Associates file with group
   - Marks uploader as seeder for all pieces

4. **Client starts peer server** (peer_server.cpp)
   - Listens on specified port
   - Serves pieces to other peers

### Download Process

1. **Client requests metadata**
   - Sends: `download file <group_id> <filename>`

2. **Tracker responds with JSON**
   ```json
   {
     "filename": "myfile.txt",
     "filesize": 1048576,
     "fileSha1": "abc123...",
     "pieceCount": 3,
     "pieceSha1": ["hash1", "hash2", "hash3"],
     "seeders": [
       {"uid": "alice", "ip": "127.0.0.1", "port": 6001}
     ]
   }
   ```

3. **Client downloads pieces** (download.cpp)
   - For each piece:
     - Connects to a seeder
     - Sends: `GET_PIECE <filename> <piece_index>`
     - Receives piece data
     - Verifies SHA1 hash
     - Writes to file

4. **Seeder serves piece** (peer_server.cpp)
   - Receives GET_PIECE request
   - Reads piece from local file
   - Sends piece data to requester

## Protocol Details

### Tracker-Client Protocol

```
Client -> Tracker: "download file group1 myfile.txt\n"
Tracker -> Client: "META {json_metadata}\n"
```

### Peer-Peer Protocol

```
Downloader -> Seeder: "GET_PIECE myfile.txt 0\n"
Seeder -> Downloader: [binary piece data]
```

## Error Handling

The system handles:
- **Tracker failover**: Automatically reconnects to backup tracker
- **Hash verification**: Rejects corrupted pieces
- **Missing seeders**: Reports when no seeders available
- **Network errors**: Retries with different peers

## Key Features

1. **Piece-by-piece download**: Files split into 512KB pieces
2. **Hash verification**: Every piece verified with SHA1
3. **Multiple seeders**: Can download from any available seeder
4. **Progress tracking**: Real-time download progress
5. **Automatic sharing**: Downloaded files automatically shared
6. **Concurrent downloads**: Multiple files can download simultaneously

## Troubleshooting

### "No seeders available"
- Ensure the uploader is still running and logged in
- Check if uploader stopped sharing the file

### "Hash mismatch"
- File may be corrupted on seeder's side
- Try downloading from a different seeder

### "Connection refused"
- Check peer port is correct
- Ensure firewall allows connections
- Verify peer server is running

### Download stuck at 0%
- Check network connectivity
- Verify seeder's IP and port are correct
- Ensure file exists on seeder's system

## Testing Checklist

- [ ] Upload a small text file
- [ ] List files in group
- [ ] Download the file to different location
- [ ] Verify downloaded file matches original
- [ ] Check download progress
- [ ] Test with multiple seeders
- [ ] Test stop share functionality
- [ ] Test with large files (multiple pieces)
- [ ] Test tracker failover during download
- [ ] Test concurrent downloads

## Important Notes

1. **Peer ports must be unique** - Each client needs a different peer port
2. **Files persist only while client runs** - Shutdown stops sharing
3. **Login required** - Must login before upload/download
4. **Group membership** - Must be group member to access files
5. **SHA1 verification** - Ensures data integrity across network