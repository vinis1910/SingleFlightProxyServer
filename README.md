# SingleFlight Proxy Server

PostgreSQL proxy server that implements the SingleFlight pattern, avoiding duplicate executions of identical queries.

## Requirements

- **Compiler**: GCC/G++ with C++20 support or higher (GCC 10+ recommended)
- **Boost.Asio**: Boost library
- **spdlog**: Fast C++ logging library (header-only)
- **System**: WSL (recommended), Windows (MinGW/MSYS2), Linux or macOS

### Installation on Linux (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install build-essential libboost-all-dev libspdlog-dev
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

## Project Structure

```
SingleFlight/
├── src/
│   ├── main.cpp                 # Entry point
│   ├── ProxyServer/
│   │   ├── ProxyServer.hpp      # Proxy server header
│   │   └── ProxyServer.cpp      # Proxy server implementation
│   └── Session/
│       ├── Session.hpp          # Session header
│       └── Session.cpp          # Session implementation
├── build/                       # Object files (generated)
├── bin/                         # Executable (generated)
└── Makefile                     # Build file
```

## Logging

The project uses `spdlog` for logging. Log levels can be configured in `src/main.cpp`:

```cpp
spdlog::set_level(spdlog::level::debug);  // debug, info, warn, error
```

## Next Steps

This is a basic proxy. To implement SingleFlight:

1. Intercept queries (type 'Q' in PostgreSQL protocol)
2. Extract SQL from the query
3. Check if the same query is already being executed
4. Share results between sessions that make the same query

## License

This project is an educational example.
