# SingleFlight Proxy Server

PostgreSQL proxy server that implements the SingleFlight pattern, avoiding duplicate executions of identical queries.

## Requirements

- **Compiler**: GCC/G++ with C++20 support or higher (GCC 10+ recommended)
- **Boost.Asio**: Boost library
- **spdlog**: Fast C++ logging library (header-only)
- **System**: Linux, Windows (WSL/MinGW/MSYS2),  or macOS

### Installation on Linux (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install build-essential libboost-all-dev libspdlog-dev libssl-dev
```

**Note:** If Boost is in another location, set the variable:
```bash
export BOOST_ROOT=/path/to/boost
make
```

## Compilation

### Build the project

```bash
make
```

### Build and run

```bash
make run
```

### Clean compiled files

```bash
make clean
```

### Rebuild everything

```bash
make rebuild
```

## Usage

### Run the proxy

```bash
./bin/singleflight-proxy
```

By default, the proxy:
- **Listens on port**: 6000
- **Redirects to**: PostgreSQL at `127.0.0.1:5432`

### Connect to the proxy

Configure your PostgreSQL client to connect to:
```
Host: 127.0.0.1
Port: 6000
```

The proxy will automatically redirect to the real PostgreSQL on port 5432.

## Configuration

To change the port or database host, edit `src/main.cpp`:

```cpp
unsigned short local_port = 6000;
std::string db_host = "127.0.0.1";
unsigned short db_port = 5432;
```

## Available Make Commands

- `make` - Build the project
- `make clean` - Remove compiled files
- `make rebuild` - Clean and rebuild everything
- `make run` - Build and run the program
- `make help` - Show help
