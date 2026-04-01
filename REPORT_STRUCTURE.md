# Report Structure & Content Guide
## CE4013/CZ4013/SC4051 Distributed Systems — Banking System Project
> Max 12 pages excluding cover. Deadline: 11:59pm, April 2, 2026.
> This file is a detailed writing guide. Each section has draft paragraphs you can adapt directly into your report, plus notes on what is still missing in the code.

---

## COVER PAGE (not counted in 12 pages)

**Content to include:**
- Course code: CE4013 / CZ4013 / SC4051 Distributed Systems
- Project title: Design and Implementation of a Distributed Banking System
- Session: 2025/2026 Semester 2
- Names of all group members (exactly as on matriculation cards)
- Matriculation numbers
- Percentage of work done by each member (must add to 100%)

**Missing:** Nothing code-wise. Fill in manually.

---

## Section 1 — Introduction (~0.5 page)

**Draft paragraphs you can adapt:**

> This report describes the design and implementation of a distributed banking system built as a course project for CE4013/CZ4013/SC4051 Distributed Systems. The system follows a client-server architecture in which all communication between the client and server is carried out over UDP (User Datagram Protocol), as opposed to TCP. UDP was chosen as the transport protocol because it is connectionless and unreliable, which provides an opportunity to study and implement fault-tolerance mechanisms such as timeouts, retransmissions, duplicate detection, and invocation semantics at the application layer.

> Both the client and server programs are implemented in C++. The server maintains all bank account information in memory during execution. The client provides a text-based console interface through which users can invoke banking services. All messages exchanged between the client and server use a custom binary protocol with manually implemented marshalling and unmarshalling routines. No existing RMI, RPC, CORBA, or serialization libraries are used.

> The system implements six banking operations: (1) opening a new account, (2) closing an existing account, (3) depositing money, (4) withdrawing money, (5) monitoring account updates via a callback mechanism, and (6) viewing account information. In addition, a seventh operation — transferring money between two accounts — has been implemented. Among the additional operations, "View Account" is idempotent (does not modify state) and "Transfer" is non-idempotent (modifies balances and produces different results on repeated execution).

> Two invocation semantics are supported: at-least-once and at-most-once. The user can select the desired semantic via a command-line argument when starting the client. The at-most-once implementation uses a server-side reply cache indexed by a composite request identifier (request ID, client IP address, and client port number) to filter duplicate requests and prevent re-execution.

> The remainder of this report is organized as follows: Section 2 describes the overall system architecture. Section 3 presents the message format design including marshalling strategies. Section 4 explains the implementation of each banking service. Section 5 discusses the fault-tolerance mechanisms and invocation semantics. Section 6 describes the experiments conducted to compare the two semantics. Section 7 lists the assumptions and design decisions. Section 8 concludes the report.

**Missing in code:** Nothing. This section can be written now.

---

## Section 2 — System Architecture (~1 page)

**Draft paragraphs you can adapt:**

### 2.1 Overall Architecture

> The system consists of two programs: a server program and a client program, communicating via UDP datagrams. The server listens on a specified port for incoming requests. The client is configured with the server's IP address and port number at startup. There can be multiple client instances running concurrently on different machines, all communicating with the same server.

> The server is single-threaded. It processes one request at a time in a loop: receive a datagram, parse and validate the header, dispatch to the appropriate handler function, construct a reply, and send it back. As specified in the lab manual, we assume requests from different clients are well-separated in time, so there is no need for multi-threading at the server.

> The client is also single-threaded. It presents a numbered text menu to the user. When the user selects an operation, the client collects the required inputs, marshals them into a binary request packet, sends it to the server, and waits for a reply. If the reply does not arrive within the timeout period, the client may retransmit the request depending on the selected invocation semantic. When the user selects the "Monitor" option, the client enters a blocking loop where it waits for callback notifications from the server until the monitoring interval expires.

### 2.2 Module Structure

> The codebase is organized into three directories:

> **`common/` — Shared definitions used by both client and server:**
> - `protocol.h`: Defines the protocol version number (`version_number = 1`), and enumerations for `Opcode` (7 operation types), `Semantics` (AT_LEAST_ONCE = 0, AT_MOST_ONCE = 1), `Status` (SUCCESS = 0, ERROR = 1), and `Currency` (SGD = 0, RM = 1). These enums are stored as `uint8_t` in transmitted messages to keep the binary representation compact and platform-independent.
> - `marshalling.h`: Contains the `ByteWriter` and `ByteReader` structs that handle serialization and deserialization of all primitive types. `ByteWriter` appends bytes to a `std::vector<uint8_t>` buffer. `ByteReader` reads from a raw `const uint8_t*` pointer with bounds checking. Both handle network byte order conversion via `htonl()`/`ntohl()`/`htons()`/`ntohs()` for 16-bit and 32-bit integers, and a custom `hton_u64()` function for 64-bit integers and floating-point values.

> **`server/` — Server-side code:**
> - `main.cpp`: Entry point. Parses the port number from command-line arguments, constructs a `ServerApp` object, and calls `run()`.
> - `server_app.h` / `server_app.cpp`: The `ServerApp` class. Contains the main receive loop in `run()`. Owns the `accounts_` hash map (`std::unordered_map<uint32_t, Account>`), the `reply_cache_` hash map for at-most-once duplicate detection, the `MonitorService` instance, and the `next_account_` counter for auto-assigning account IDs. Has one handler method per operation (e.g., `handle_open_account()`, `handle_close_account()`, etc.) plus utility methods `cache_reply_if_needed()` and `send_buffer_to_client()`.
> - `models.h`: Data structures. The `Account` struct stores account ID (uint32_t), holder name (string), password (string), currency (Currency enum), and balance (double). The `ReqID` struct stores the composite key (rid, ip, port) used for at-most-once deduplication, with an `operator==` and a `ReqIDHash` functor for use in `std::unordered_map`. The `MonitorClient` struct stores a `sockaddr_in` address and an `expiry` time point.
> - `monitor_service.h` / `monitor_service.cpp`: The `MonitorService` class. Maintains `monitor_list_` (a `std::vector<MonitorClient>`). `register_or_refresh()` either adds a new client or updates an existing client's expiry time. `prune_expired_monitors()` removes clients whose interval has expired. `notify_monitors()` constructs a callback notification packet and sends it to all active monitor clients.
> - `reply_utils.h` / `reply_utils.cpp`: A helper function `build_reply_header(Status, rid)` that constructs the standard 8-byte reply header (version, status, padding, rid) and returns a `ByteWriter` so the caller can append operation-specific payload.

> **`client/` — Client-side code:**
> - `client.cpp`: Single-file client implementation. The `main()` function parses command-line arguments (server IP, server port, semantic), creates a UDP socket with a 1-second receive timeout, and enters the menu loop. Six standalone handler functions (`handle_open_account`, `handle_close_account`, `handle_deposit_or_withdrawal`, `handle_monitor`, `handle_view_account`, `handle_transfer`) each collect user input via stdin, build a request packet using `ByteWriter`, and implement the send-receive-retry loop. A global static `next_rid` counter provides monotonically increasing request IDs.

### 2.3 How to Run

```
# Compile (example using g++)
g++ -o server_app server/main.cpp server/server_app.cpp server/monitor_service.cpp \
    server/reply_utils.cpp common/marshalling.cpp -std=c++17
g++ -o client_app client/client.cpp common/marshalling.cpp -std=c++17

# Start server (port as argument)
./server_app 2222

# Start client (server IP, port, semantic as arguments)
./client_app 192.168.1.5 2222 0    # 0 = at-least-once
./client_app 192.168.1.5 2222 1    # 1 = at-most-once
```

### 2.4 Architecture Diagram

> Include a diagram similar to this (draw properly in your report tool):

```
+-------------------+         UDP          +-------------------+
|   Client A        | ----- request -----> |                   |
|  (at-least-once)  | <---- reply -------- |                   |
+-------------------+                      |                   |
                                           |     Server        |
+-------------------+         UDP          |                   |
|   Client B        | ----- request -----> |  - accounts_ map  |
|  (at-most-once)   | <---- reply -------- |  - reply_cache_   |
+-------------------+                      |  - monitor_list_  |
                                           |                   |
+-------------------+         UDP          |                   |
|   Client C        | <--- callback ------ |  MonitorService   |
|  (monitoring)     |    (unsolicited)     |                   |
+-------------------+                      +-------------------+
```

> The diagram shows that multiple clients can connect to the same server. Client C has registered for monitoring and receives unsolicited callback messages from the server whenever any account is modified by any client.

**Missing in code:** Nothing. This section can be written now.

---

## Section 3 — Message Format Design (~2 pages)

> This is one of the most important sections. The spec explicitly says: "you are required to design the structures for request and reply messages, and describe your design in the report."

**Draft paragraphs you can adapt:**

### 3.1 Marshalling Strategy

> All messages transmitted between the client and server are sequences of raw bytes. We implement custom marshalling and unmarshalling using two helper structs defined in `marshalling.h`: `ByteWriter` (for serialization) and `ByteReader` (for deserialization).

> **Integer encoding:** All multi-byte integers are transmitted in network byte order (big-endian, Most Significant Byte first). For 16-bit and 32-bit integers, we use the standard C library functions `htons()`/`ntohs()` and `htonl()`/`ntohl()` respectively. For 64-bit integers, we implement a custom `hton_u64()` function that composes two `htonl()` calls on the upper and lower 32-bit halves, swapping their positions on little-endian platforms. This ensures correct byte ordering regardless of the host architecture.

> **Floating-point encoding:** The account balance and monetary amounts are `double` (IEEE 754, 64-bit). Rather than converting to a string or fixed-point representation, we bit-cast the `double` to a `uint64_t` using `memcpy` (function `double_to_u64()`). This preserves the exact binary representation. The resulting `uint64_t` is then byte-swapped to network order using `hton_u64()`. On the receiving end, the reverse process is applied: `ntoh_u64()` followed by `u64_to_double()`. This approach works correctly when both client and server use IEEE 754 floating-point representation, which is the case for all modern x86/x64 platforms.

> **String encoding:** Strings (such as account holder names and passwords) have variable lengths. We encode them with a 2-byte big-endian length prefix (`uint16_t`), followed by the raw bytes of the string (no null terminator). This allows the receiver to know exactly how many bytes to read for each string. The maximum string length is 65,535 bytes (the maximum value of `uint16_t`), which is more than sufficient for names and passwords. The `ByteWriter::str_with_len()` method handles serialization, and `ByteReader::str_u16len()` handles deserialization.

> **Enumerated type encoding:** Enumerations (`Opcode`, `Semantics`, `Status`, `Currency`) are all defined as `uint8_t` in the code, so they are transmitted as single bytes with no byte-order concerns.

### 3.2 General Request Message Header

> All client-to-server request messages share a fixed 8-byte header:

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0 | 1 byte | `version` | `uint8_t` | Protocol version number. Currently hardcoded to `1`. The server checks this and returns an error if it does not match, allowing for future protocol evolution. |
| 1 | 1 byte | `opcode` | `uint8_t` | Identifies which operation the client is requesting. Values 1 through 7 correspond to the seven banking operations. |
| 2 | 1 byte | `semantic` | `uint8_t` | The invocation semantic. `0` = at-least-once, `1` = at-most-once. Included in every request so the server knows whether to check the reply cache. |
| 3 | 1 byte | `padding` | `uint8_t` | Reserved byte, always set to `0`. Ensures the 4-byte `rid` field starts at a 4-byte-aligned offset, which can be beneficial for memory alignment on some architectures. |
| 4-7 | 4 bytes | `rid` | `uint32_t` | Request identifier in network byte order. A monotonically increasing counter generated by the client. Combined with the client's IP address and port number (obtained from the UDP datagram header by the server), this forms a globally unique request identifier used for duplicate detection. |

> After the 8-byte header, the remaining bytes contain the operation-specific payload, which varies in structure and length depending on the opcode.

> **Why we designed it this way:** The fixed-size header means the server can parse the first 8 bytes of any incoming datagram to determine the protocol version, the requested operation, the invocation semantic, and the request ID — all before reading any operation-specific fields. This makes the dispatch logic in `ServerApp::run()` clean and uniform. The version check is done first, so a version mismatch can be reported without parsing any further. The semantic check is done next, so duplicate requests under at-most-once can be returned from cache immediately without invoking the handler.

### 3.3 Per-Operation Request Payloads

> Each operation appends its own fields after the 8-byte header. Below is the detailed layout for each:

**OPEN_ACCOUNT (opcode = 1):**

| Field | Type | Size | Encoding | Description |
|-------|------|------|----------|-------------|
| `name` | string | 2 + N bytes | u16 length prefix + raw bytes | Name of the account holder. |
| `password` | string | 2 + M bytes | u16 length prefix + raw bytes | Password for the account. |
| `currency` | `uint8_t` | 1 byte | `0` = SGD, `1` = RM | Currency type for the new account. |
| `initial_balance` | `double` | 8 bytes | bit-cast to u64, network order | Starting balance. Must be >= 0. |

> Example: Opening an account for "Alice" with password "pass123", currency SGD, balance 500.0 would produce a total message of 8 (header) + 2+5 (name) + 2+7 (password) + 1 (currency) + 8 (balance) = 33 bytes.

**CLOSE_ACCOUNT (opcode = 2):**

| Field | Type | Size | Encoding | Description |
|-------|------|------|----------|-------------|
| `name` | string | 2 + N bytes | u16 length prefix + raw bytes | Must match the account holder's name. |
| `password` | string | 2 + M bytes | u16 length prefix + raw bytes | Must match the account's password. |
| `account_id` | `uint32_t` | 4 bytes | network byte order | The account number to close. |

**DEPOSIT (opcode = 3) / WITHDRAW (opcode = 4):**

> Both operations share the same payload structure. The opcode distinguishes them.

| Field | Type | Size | Encoding | Description |
|-------|------|------|----------|-------------|
| `name` | string | 2 + N bytes | u16 length prefix + raw bytes | Must match account holder. |
| `password` | string | 2 + M bytes | u16 length prefix + raw bytes | Must match account password. |
| `account_id` | `uint32_t` | 4 bytes | network byte order | The target account number. |
| `currency` | `uint8_t` | 1 byte | `0` = SGD, `1` = RM | Must match the account's currency. Prevents depositing RM into an SGD account. |
| `amount` | `double` | 8 bytes | bit-cast to u64, network order | Amount to deposit or withdraw. Must be > 0. |

**MONITOR (opcode = 5):**

| Field | Type | Size | Encoding | Description |
|-------|------|------|----------|-------------|
| `interval_seconds` | `uint32_t` | 4 bytes | network byte order | How long to monitor, in seconds. Minimum 10. |

> This is the simplest request payload — only one field. The server uses the source IP and port from the UDP datagram to identify the monitoring client; no name or account ID is needed because the monitor watches all accounts.

**VIEW_ACCOUNT (opcode = 6) — Extra idempotent operation:**

| Field | Type | Size | Encoding | Description |
|-------|------|------|----------|-------------|
| `name` | string | 2 + N bytes | u16 length prefix + raw bytes | Must match account holder. |
| `password` | string | 2 + M bytes | u16 length prefix + raw bytes | Must match account password. |
| `account_id` | `uint32_t` | 4 bytes | network byte order | The account to view. |

**TRANSFER (opcode = 7) — Extra non-idempotent operation:**

| Field | Type | Size | Encoding | Description |
|-------|------|------|----------|-------------|
| `sender_name` | string | 2 + N bytes | u16 length prefix + raw bytes | Name of the person initiating the transfer. Must match sender account holder. |
| `sender_password` | string | 2 + M bytes | u16 length prefix + raw bytes | Password for the sender's account. |
| `sender_account_id` | `uint32_t` | 4 bytes | network byte order | Source account. |
| `recipient_name` | string | 2 + P bytes | u16 length prefix + raw bytes | Name of the person receiving the transfer. Must match recipient account holder. |
| `recipient_account_id` | `uint32_t` | 4 bytes | network byte order | Destination account. |
| `amount` | `double` | 8 bytes | bit-cast to u64, network order | Amount to transfer. Must be > 0. |

> Note that only the sender's password is required — the recipient does not need to authorize incoming transfers. Both accounts must use the same currency; cross-currency transfers are not supported.

### 3.4 General Reply Message Header

> All server-to-client reply messages share a fixed 8-byte header, constructed by the `build_reply_header()` utility function:

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0 | 1 byte | `version` | `uint8_t` | Protocol version, always `1`. |
| 1 | 1 byte | `status` | `uint8_t` | `0` = SUCCESS, `1` = ERROR. Tells the client whether to parse success-specific fields or just an error message. |
| 2-3 | 2 bytes | `padding` | `uint16_t` | Reserved, always `0`. Keeps the `rid` field at offset 4 for alignment. |
| 4-7 | 4 bytes | `rid` | `uint32_t` | Echo of the request ID from the original request. The client uses this to match the reply to its outstanding request. If the `rid` in the reply does not match the expected `rid`, the client discards the reply and retries. |

> **On ERROR (status = 1):** The payload after the header is always a single u16-prefixed string containing a human-readable error message. Examples: "Account does not exist", "Wrong password", "Insufficient balance".

> **On SUCCESS (status = 0):** The payload varies per operation. Here is what each operation returns on success:

**OPEN_ACCOUNT success reply:**

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `account_id` | `uint32_t` | 4 bytes | The server-assigned account number for the newly created account. |
| `message` | string | 2 + N bytes | Confirmation text, e.g., "Account created". |

**CLOSE_ACCOUNT success reply:**

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `account_id` | `uint32_t` | 4 bytes | The account number that was closed (echoed back as confirmation). |
| `message` | string | 2 + N bytes | Confirmation text, e.g., "Account successfully closed". |

**DEPOSIT / WITHDRAW success reply:**

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `new_balance` | `double` | 8 bytes (as u64) | The updated account balance after the deposit or withdrawal. |
| `message` | string | 2 + N bytes | e.g., "Deposit successful" or "Withdrawal successful". |

**MONITOR success reply:**

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `message` | string | 2 + N bytes | Confirmation text, e.g., "Monitor registration successful". No additional data fields — the actual account updates arrive later as unsolicited callback messages. |

**VIEW_ACCOUNT success reply:**

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `currency` | `uint8_t` | 1 byte | The account's currency type. |
| `balance` | `double` | 8 bytes (as u64) | The current balance. |
| `message` | string | 2 + N bytes | e.g., "Account info retrieved successfully". |

**TRANSFER success reply:**

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `sender_new_balance` | `double` | 8 bytes (as u64) | Sender's updated balance after deduction. |
| `recipient_new_balance` | `double` | 8 bytes (as u64) | Recipient's updated balance after addition. |
| `message` | string | 2 + N bytes | e.g., "Transfer successful". |

### 3.5 Monitor Callback Message Format

> When a client registers for monitoring, the server records the client's IP address and port. During the monitoring interval, whenever any account is modified (by any operation from any client), the server sends an unsolicited callback notification to all active monitoring clients. The callback message has a different structure from regular replies:

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0 | 1 byte | `version` | `uint8_t` | Always `1`. |
| 1 | 1 byte | `opcode` | `uint8_t` | The operation that triggered the update (e.g., OPEN_ACCOUNT=1, DEPOSIT=3, etc.). This tells the monitoring client what kind of change was made. |
| 2-3 | 2 bytes | `padding` | `uint16_t` | Always `0`. |
| 4+ | variable | `name` | u16-string | Account holder's name. |
| + | 4 bytes | `account_id` | `uint32_t` | The affected account number. |
| + | 1 byte | `currency` | `uint8_t` | The account's currency type. |
| + | 8 bytes | `balance` | `uint64_t` | The new balance after the update (bit-cast double, network order). |
| + | variable | `message` | u16-string | Human-readable description of the update (e.g., "Account opened", "Withdrawal successful", "Transfer in successful"). |

> **Key difference from regular replies:** Callback messages do not include a `rid` field because they are unsolicited pushes from the server, not responses to specific client requests. The client identifies them by the fact that the opcode field contains an operation code rather than a status code.

> **How the client distinguishes callbacks from replies:** During monitoring mode, the client enters a dedicated receive loop. Any datagram received during this period is parsed as a callback message. The client checks the `version` byte and discards messages with an unexpected version.

**Missing in code:** Nothing. This section can be fully written from the code.

---

## Section 4 — Service Implementations (~2 pages)

> For each service, describe: what inputs the user provides, how the client constructs the request, how the server processes it (including all validation steps and error cases), what the server returns, and how the client presents the result.

**Draft paragraphs you can adapt:**

### 4.1 Open Account

> The user is prompted to enter their name, a password of their choice, the currency type (SGD or RM), and an initial deposit amount. The client marshals these into an OPEN_ACCOUNT request and sends it to the server.

> **Server-side processing (`handle_open_account`):** The server first validates the input: the name must not be empty, the password must not be empty, and the initial balance must be non-negative. If any validation fails, an error reply is sent with a descriptive message. Otherwise, the server creates a new `Account` struct, assigns the next available auto-incremented account ID (starting from 1), and inserts it into the `accounts_` hash map. The server then calls `monitor_service_.notify_monitors()` to push a callback notification to all registered monitoring clients. Finally, a success reply containing the new account ID is sent back to the client.

> **Client-side display:** On success, the client prints the account ID so the user can record it for future operations. On error, the error message from the server is displayed.

> **Idempotency note:** OPEN_ACCOUNT is non-idempotent. Executing it twice with the same inputs creates two separate accounts with different account IDs, consuming two account number slots. Under at-least-once semantics, if the reply is lost and the client retries, a duplicate account may be created.

### 4.2 Close Account

> The user provides their name, password, and the account ID they wish to close.

> **Server-side processing (`handle_close_account`):** The server looks up the account by ID in the `accounts_` map. If not found, it returns "Account does not exist". If the stored name does not match the provided name, it returns "Account does not belong to this user". If the password is wrong, it returns "Wrong password". If all checks pass, the server saves a copy of the account data (needed for the callback notification), erases the account from the map, notifies monitors, and returns a success reply.

> **Client-side display:** On success, the client prints the closed account ID as confirmation. On error, the error message is displayed.

> **Idempotency note:** CLOSE_ACCOUNT is non-idempotent — the first execution deletes the account and returns success, but a second execution with the same inputs returns "Account does not exist" because the account has already been removed. Under at-least-once, a retransmitted close request after a lost reply would return an error to the user even though the account was successfully closed.

### 4.3 Deposit / Withdraw

> The user provides their name, password, account ID, currency type, and the amount to deposit or withdraw. Both operations share the same handler function (`handle_deposit_or_withdrawal`) with the `Opcode` parameter distinguishing them.

> **Server-side processing:** Same authentication checks as Close Account (account existence, name match, password match). Additionally, the server checks that the provided currency matches the account's currency (to prevent depositing RM into an SGD account), and that the amount is strictly greater than zero. For withdrawals, an additional check ensures the account balance is sufficient. If all validations pass, the balance is updated: `balance += amount` for deposits, `balance -= amount` for withdrawals. The monitor service is notified, and the new balance is returned to the client.

> **Error cases (6 total):** Account does not exist; account does not belong to user; wrong password; currency mismatch; amount <= 0; insufficient balance (withdraw only).

> **Client-side display:** On success, the message (e.g., "Deposit successful") and the new balance are printed.

> **Idempotency note:** DEPOSIT is non-idempotent — depositing $100 twice results in $200 added. WITHDRAW is also non-idempotent — withdrawing $100 twice deducts $200. Under at-least-once semantics, a retried deposit or withdrawal after a lost reply would incorrectly modify the balance a second time. This is the primary scenario used in our experiments (Section 6) to demonstrate the danger of at-least-once for non-idempotent operations.

### 4.4 Monitor (Callback Registration)

> The user enters a monitoring interval in seconds (minimum 10 seconds).

> **Server-side processing (`handle_monitor`):** The server first calls `prune_expired_monitors()` to remove any monitoring registrations that have passed their expiry time. If the interval is less than 10 seconds, an error is returned. Otherwise, `register_or_refresh()` is called: if this client (identified by IP + port) is already in the monitor list, its expiry is updated; if not, a new `MonitorClient` entry is added with `expiry = now + interval_seconds`.

> **Callback delivery (`notify_monitors`):** This method is called at the end of every state-modifying operation (open, close, deposit, withdraw, transfer). It first prunes expired monitors, then iterates over all remaining monitor clients. For each one, it constructs a callback message containing the affected account's name, ID, currency, new balance, and a description string, and sends it via `sendto()`.

> **Client-side behavior after registration:** After receiving the success reply, the client enters a timed loop:
> ```
> auto end_time = steady_clock::now() + seconds(interval_in_seconds);
> while(steady_clock::now() < end_time) {
>     bytes = recvfrom(...);  // blocks up to 1 second (SO_RCVTIMEO)
>     if (bytes < 0) continue;  // timeout, loop again
>     // parse and print callback
> }
> ```
> Each received callback is parsed and displayed in a formatted block showing the triggering operation, account name, account ID, currency, balance, and message. When the interval expires, the client prints "Monitor interval expired" and returns to the main menu.

> **Concurrency:** Multiple clients can register for monitoring simultaneously. The `monitor_list_` is a `std::vector<MonitorClient>`, and every callback is sent to all non-expired entries. Since the server is single-threaded, there are no race conditions on the monitor list.

### 4.5 View Account — Extra Idempotent Operation

> The user provides their name, password, and account ID. The server looks up the account, performs the same authentication checks as other operations, and on success, returns the account's currency type and current balance.

> **Why it is idempotent:** VIEW_ACCOUNT is a pure read operation. It does not modify any server state — no balances are changed, no accounts are created or deleted, no side effects occur. Executing the same VIEW_ACCOUNT request any number of times always produces the same result (assuming no concurrent modifications to the account by other operations). This makes it safe under at-least-once semantics: even if the client retransmits due to a lost reply, the server simply re-reads the same data and returns the same answer.

> **Design rationale:** We chose "View Account" as the idempotent operation because it is the most natural read-only banking operation. Checking your balance is something users need to do frequently, and it inherently does not change the system state. This contrasts with the spec-required operations (open, close, deposit, withdraw) which all modify the account data.

### 4.6 Transfer — Extra Non-Idempotent Operation

> The user provides: sender name, sender password, sender account ID, recipient name, recipient account ID, and transfer amount.

> **Server-side processing (`handle_transfer`):** This is the most complex handler, with 8 validation checks performed in this order:
> 1. Amount must be > 0
> 2. Sender account must exist
> 3. Recipient account must exist
> 4. Sender name must match the sender account holder
> 5. Sender password must be correct
> 6. Recipient name must match the recipient account holder
> 7. Sender and recipient cannot be the same account
> 8. Both accounts must use the same currency (no cross-currency transfers)
> 9. Sender must have sufficient balance (balance >= amount)
>
> If all checks pass, the server atomically (within the single-threaded context) subtracts the amount from the sender's balance and adds it to the recipient's balance. Two monitor callbacks are sent: one for the sender account ("Transfer out successful") and one for the recipient account ("Transfer in successful"). The reply includes both new balances.

> **Why it is non-idempotent:** If a TRANSFER of $100 from account A to account B is executed twice, account A loses $200 and account B gains $200, rather than the intended $100 transfer. The final state depends on how many times the operation is executed. This is different from the first execution's result. Under at-least-once semantics, if the server executes the transfer and the reply is lost, the client retransmits, and the server executes it again — resulting in a double transfer. Under at-most-once semantics, the reply cache prevents re-execution.

> **Design rationale:** We chose "Transfer" as the non-idempotent operation because it modifies two accounts simultaneously, making the double-execution problem especially visible: both accounts end up with incorrect balances. It is also a realistic banking operation that clearly demonstrates why at-most-once semantics are important for financial transactions.

**Missing in code:** Nothing. This section can be fully written now.

---

## Section 5 — Fault Tolerance & Invocation Semantics (~2-3 pages)

> This is the core distributed systems section. Explain the theory, then explain exactly how your code implements each semantic.

**Draft paragraphs you can adapt:**

### 5.1 Background: Why Fault Tolerance is Needed

> UDP is an unreliable transport protocol — packets can be lost, duplicated, or delivered out of order. In our system, either the client's request or the server's reply can be lost in transit. Without any fault-tolerance mechanism, a lost request would cause the client to wait indefinitely, and a lost reply would leave the client unaware that the server successfully processed the request.

> To handle packet loss, the client implements a **timeout-and-retransmit** mechanism. The client socket has a receive timeout of 1 second (`SO_RCVTIMEO` set in `setsockopt()`). If no reply is received within 1 second, `recvfrom()` returns -1, and the client may retransmit the request.

> However, retransmission introduces a new problem: **duplicate execution**. If the server received and processed the original request, but the reply was lost, the server will receive the retransmitted request and process it again. For idempotent operations this is harmless, but for non-idempotent operations it produces incorrect results. Two invocation semantics address this differently.

### 5.2 At-Least-Once Semantics

> **Definition:** The client retransmits until it gets a reply. The operation may be executed more than once at the server.

> **Implementation in our code:**
> - When the client is started with semantic argument `0`, the `Semantics` enum is set to `AT_LEAST_ONCE`.
> - In every client handler function, `attempts` is set to `3`: `int attempts = semantic == Semantics::AT_LEAST_ONCE ? 3 : 1;`
> - The client loops up to 3 times. In each iteration: (1) send the request via `sendto()`, (2) wait for reply via `recvfrom()` with 1-second timeout, (3) if timeout, print "Timeout on attempt N" and try again.
> - If after 3 attempts no reply is received, the client prints "No reply from server" and returns to the menu.
> - On the server side, the `semantic` byte in the request header is `0`. `cache_reply_if_needed()` checks: `if (semantic == AT_MOST_ONCE)` — since it's not, no reply is cached. So a retransmitted request is processed as a fresh request.

> **Consequence:** If the server processed the original request and its reply was lost, retransmission causes the server to execute the same operation a second time. For non-idempotent operations, this is incorrect:
> - WITHDRAW $100 from an account with $1000: first execution gives $900, retransmission gives $800. The client eventually sees $800 but expected $900.
> - TRANSFER $100 from A to B: first execution moves $100, retransmission moves another $100. Both accounts end up with wrong balances.

> **When it's acceptable:** For idempotent operations like VIEW_ACCOUNT, the server simply re-reads the same data, so re-execution is harmless.

### 5.3 At-Most-Once Semantics

> **Definition:** The server maintains a history of past replies. If a duplicate request arrives, the cached reply is returned without re-executing the operation. The operation is guaranteed to execute at most once.

> **Implementation in our code:**

> **Server-side (duplicate detection and reply caching):**
> - The server maintains `reply_cache_` of type `std::unordered_map<ReqID, std::vector<uint8_t>, ReqIDHash>`.
> - `ReqID` is a struct containing three fields: `uint32_t rid` (the request ID), `uint32_t ip` (the client's IP address from the UDP datagram), and `uint16_t port` (the client's port). Together, these three fields uniquely identify a request across all clients.
> - When a request arrives with `semantic == AT_MOST_ONCE`, the server first searches the reply cache: `auto reply = reply_cache_.find(req_id);`. If found, the server logs "duplicate request: X. resending cached reply" and sends the cached reply bytes directly via `send_buffer_to_client()`, without invoking any handler function.
> - If not found, the request is processed normally. After the handler constructs the reply, `cache_reply_if_needed()` stores the complete reply byte buffer in `reply_cache_[req_id]`.
> - The cache stores the raw reply bytes (not parsed data), so re-sending a cached reply is trivial — just `sendto()` the stored byte vector.

> **Client-side:**
> - The `semantic` byte in the request header is set to `1`, telling the server to use at-most-once processing.

> **Hash function:** `ReqIDHash` combines the three `ReqID` fields using XOR with bit shifts: `return (size_t)k.rid ^ ((size_t)k.ip << 1) ^ ((size_t)k.port << 16);`. This distributes keys reasonably well across the hash map buckets.

> **Stale reply matching protection:** The client checks that the `rid` in each received reply matches the expected `rid` for the current request: `if(reply_rid != current_rid) { ... continue; }`. This prevents the client from accepting a stale reply from a previous request as the response to the current one.

### 5.4 Comparison of the Two Semantics

| Aspect | At-Least-Once | At-Most-Once |
|--------|---------------|--------------|
| Client retransmits on timeout? | Yes (up to 3 attempts) | Yes (should also retry — see note below) |
| Server detects duplicates? | No | Yes, via `reply_cache_` |
| Server caches replies? | No | Yes |
| Safe for idempotent operations? | Yes | Yes |
| Safe for non-idempotent operations? | No — may execute twice | Yes — cached reply returned for duplicates |
| Server memory overhead | None | Grows with number of unique requests |
| Latency overhead | None | One hash map lookup per request |

### 5.5 Message Loss Simulation

**⚠️ NOT YET IMPLEMENTED. Needs to be added before experiments can be run.**

> To test the invocation semantics, we simulate packet loss by randomly dropping outgoing messages. The drop probability is configurable via a command-line argument.

> **How it should work:**
> - On the **client side**, before each `sendto()` call, the program generates a random number between 0.0 and 1.0 using `std::uniform_real_distribution`. If the random number is below the configured drop rate, the `sendto()` is skipped and the program prints "[SIMULATED] Request packet dropped" to the console. The retry logic then kicks in as if the network lost the packet.
> - On the **server side**, the same mechanism is applied in `send_buffer_to_client()`. If the random number is below the drop rate, the reply is not sent and the program prints "[SIMULATED] Reply packet dropped". This simulates a scenario where the server processes the request but the reply never reaches the client.
> - The random number generator is seeded once at startup using `std::random_device` for non-deterministic behavior, or a fixed seed for reproducible experiments.

**What to prompt to implement this:** "Add a configurable simulated packet drop rate to the client and server. Accept it as a command-line argument (e.g., `./server_app 2222 0.3` for 30% drop rate). On the client side, randomly skip `sendto()` calls. On the server side, randomly skip `send_buffer_to_client()` calls. Use `std::mt19937` with `std::uniform_real_distribution<double>`. Print a message whenever a simulated drop occurs."

**⚠️ ALSO MISSING — At-Most-Once client retries:**
Currently the code sets `attempts = 1` for AT_MOST_ONCE, meaning the client never retransmits in at-most-once mode. This is incorrect for demonstration purposes — the whole point of the server-side reply cache is to handle retransmissions safely. The client should retry in BOTH modes. The difference between the two semantics is what the SERVER does (cache or no cache), not what the CLIENT does.

**What to prompt to fix:** "In client.cpp, change the retry logic so that `attempts = 3` for BOTH at-least-once and at-most-once. The server-side reply cache is what makes at-most-once work, not the client suppressing retries."

**⚠️ ALSO MISSING — Server request/reply logging:**
The lab spec says: "The received requests and the produced replies should be printed on the screen." Currently the server only logs cache hits and errors.

**What to prompt to fix:** "Add server-side logging in ServerApp::run() and in each handler. When a request arrives, print the client IP, port, opcode name, rid, and semantic. When a reply is sent, print the status and a summary. Example output: `[REQ] 192.168.1.5:54321 OPEN_ACCOUNT rid=3 semantic=AT_LEAST_ONCE` and `[REPLY] rid=3 status=SUCCESS account_id=1`."

---

## Section 6 — Experiments (~2 pages)

**⚠️ Experiments CANNOT be completed until the three missing items from Section 5 are implemented (message loss simulation, at-most-once retries, server logging).**

**Once implemented, run and document the following. Draft text below:**

### 6.1 Experiment Setup

> All experiments were conducted on [describe your machines — e.g., "two laptops connected to the same local network" or "a single machine using localhost (127.0.0.1)"]. The server was started on port 2222. Two client instances were used: one for performing operations, and one for monitoring. A simulated packet drop rate of [X]% was used to reliably trigger retransmissions.

> Each experiment follows the same pattern: (1) open an account with a known initial balance, (2) perform an operation under packet loss, (3) observe the result and verify whether the final balance is correct or incorrect.

### 6.2 Experiment 1 — At-Least-Once with Non-Idempotent Operation (WITHDRAW)

> **Goal:** Demonstrate that at-least-once semantics can lead to wrong results for non-idempotent operations when packets are lost.

> **Setup:** Account opened with balance $1000.00. Drop rate set to [X]% on the server reply side (so the server executes the request but the reply is dropped).

> **Procedure:**
> 1. Start server: `./server_app 2222 0.3`
> 2. Start client with at-least-once: `./client_app 127.0.0.1 2222 0 0.0` (client drop rate 0 so we control where the loss happens)
> 3. Open account with balance $1000.
> 4. Withdraw $100.

> **What happened:**
> The server received the WITHDRAW request and executed it (balance became $900). The reply was dropped (simulated). The client timed out after 1 second and retransmitted the same request. The server received the retransmission, but since this is at-least-once, there is no duplicate detection. The server executed the withdrawal again (balance became $800). The reply was successfully delivered this time.

> **Result:** The client reported a final balance of $800.00. The correct balance should have been $900.00. **At-least-once caused the withdrawal to execute twice, resulting in an incorrect double deduction.**

> **Server console output:**
> ```
> [REQ] 127.0.0.1:12345 WITHDRAW rid=2 semantic=AT_LEAST_ONCE
> [REPLY] rid=2 status=SUCCESS balance=900.00
> [SIMULATED] Reply packet dropped
> [REQ] 127.0.0.1:12345 WITHDRAW rid=2 semantic=AT_LEAST_ONCE
> [REPLY] rid=2 status=SUCCESS balance=800.00
> ```

> *(Replace the above with actual console output captured during your experiment)*

### 6.3 Experiment 2 — At-Most-Once with Non-Idempotent Operation (WITHDRAW)

> **Goal:** Demonstrate that at-most-once semantics correctly handle non-idempotent operations even when packets are lost.

> **Setup:** Same as Experiment 1, but client started with semantic `1` (at-most-once).

> **Procedure:** Same as Experiment 1. Open account with $1000, withdraw $100 under simulated reply loss.

> **What happened:**
> The server received the WITHDRAW request and executed it (balance became $900). The reply was stored in the `reply_cache_` and then dropped (simulated). The client timed out and retransmitted. The server received the retransmission, found the matching `ReqID` in the reply cache, and returned the cached reply without re-executing the withdrawal. The reply was successfully delivered.

> **Result:** The client reported a final balance of $900.00. This is the correct result. **At-most-once prevented the double execution by returning the cached reply.**

> **Server console output:**
> ```
> [REQ] 127.0.0.1:12345 WITHDRAW rid=2 semantic=AT_MOST_ONCE
> [REPLY] rid=2 status=SUCCESS balance=900.00
> [SIMULATED] Reply packet dropped
> [REQ] 127.0.0.1:12345 WITHDRAW rid=2 semantic=AT_MOST_ONCE
> duplicate request: 2. resending cached reply
> [REPLY] rid=2 status=SUCCESS (cached)
> ```

> *(Replace with actual output)*

### 6.4 Experiment 3 — At-Least-Once with Idempotent Operation (VIEW_ACCOUNT)

> **Goal:** Show that at-least-once semantics work correctly for idempotent operations even with packet loss.

> **Setup:** Account opened with balance $1000. Reply drop simulated.

> **What happened:** Server received VIEW_ACCOUNT, read balance=$1000, reply dropped. Client retransmitted. Server received duplicate, executed VIEW_ACCOUNT again, read balance=$1000 again, reply delivered.

> **Result:** Client received correct balance ($1000.00). Re-executing a read-only operation produced the same result. **At-least-once is safe for idempotent operations.**

### 6.5 Experiment 4 — At-Least-Once with TRANSFER (Non-Idempotent)

> **Goal:** Show the double-transfer problem under at-least-once.

> **Setup:** Account A with $1000, Account B with $500. Transfer $200 from A to B.

> **What happened:** Server executed transfer (A=$800, B=$700). Reply dropped. Client retransmitted. Server executed transfer again (A=$600, B=$900). Reply delivered.

> **Result:** A has $600 instead of $800, B has $900 instead of $700. **Double transfer — incorrect.**

### 6.6 Experiment 5 — At-Most-Once with TRANSFER (Non-Idempotent)

> **Setup:** Same as Experiment 4, at-most-once semantic.

> **What happened:** Server executed transfer (A=$800, B=$700), cached reply, reply dropped. Client retransmitted. Server found cached reply, returned it. No re-execution.

> **Result:** A has $800, B has $700. **Correct.**

### 6.7 Results Summary

| # | Semantic | Operation | Type | Packet Loss | Correct? | Why |
|---|----------|-----------|------|-------------|----------|-----|
| 1 | At-least-once | WITHDRAW | Non-idempotent | Reply dropped | NO | Executed twice; balance deducted twice |
| 2 | At-most-once | WITHDRAW | Non-idempotent | Reply dropped | YES | Cached reply returned; executed once |
| 3 | At-least-once | VIEW_ACCOUNT | Idempotent | Reply dropped | YES | Re-execution reads same data |
| 4 | At-least-once | TRANSFER | Non-idempotent | Reply dropped | NO | Double transfer; both balances wrong |
| 5 | At-most-once | TRANSFER | Non-idempotent | Reply dropped | YES | Cached reply returned; executed once |

### 6.8 Discussion

> **Write these points in paragraph form for the report:**

> **At-least-once is simpler but only safe for idempotent operations.** There is no server-side overhead — no reply caching, no hash map lookups. The client simply retransmits until a reply arrives. For idempotent operations like VIEW_ACCOUNT (which only reads data without modifying it), re-execution produces the same result, so this semantic is perfectly safe. However, for non-idempotent operations like WITHDRAW and TRANSFER, re-execution changes the server state each time, leading to incorrect balances.

> **At-most-once is necessary for non-idempotent operations.** By caching replies indexed by request ID, the server can detect retransmitted requests and return the stored reply without re-executing the handler. This guarantees that the operation executes at most once. The trade-off is additional memory (the reply cache grows with each unique request) and a hash map lookup on every incoming request.

> **Reply cache growth:** In our implementation, the `reply_cache_` is never evicted. In a production system, entries would be removed after a configurable timeout period (e.g., 5 minutes), since clients are unlikely to retransmit requests that are several minutes old. Alternatively, clients could send an acknowledgement for received replies, allowing the server to evict the corresponding cache entry.

> **Choosing the right semantic:** For a banking system, correctness is critical — executing a withdrawal or transfer twice is unacceptable. Therefore, at-most-once semantics should be the default for all state-modifying operations. At-least-once may be used for read-only queries where the overhead of reply caching is undesirable, though in practice the overhead of at-most-once is minimal for low-volume systems.

> **Limitations of our experiments:** The simulated packet loss is random, so the exact behavior may vary between runs. For a more controlled experiment, a deterministic drop pattern (e.g., "drop every 2nd reply") could be used. Additionally, our experiments only test single-client scenarios; concurrent multi-client retransmissions could introduce more complex interaction patterns.

---

## Section 7 — Assumptions & Design Decisions (~0.5 page)

**Draft paragraphs you can adapt:**

> The following assumptions and design decisions were made during the implementation:

> **1. In-memory storage only.** All account data is stored in `std::unordered_map<uint32_t, Account>` within the `ServerApp` instance. There is no persistent storage to disk. If the server process terminates, all account data is lost. This is acceptable per the project specification, which states that "persistent storage of bank account information on disks is not compulsory."

> **2. Single-threaded server.** The server processes requests sequentially in a single-threaded loop. As stated in the specification, "you may assume that the requests made by the clients are well separated in time so that before finishing processing a request, the server will not receive any new request from any client." This simplifies the implementation by avoiding synchronization issues. However, the monitor callback mechanism does support multiple concurrent monitoring clients — the server simply sends callbacks to all registered clients in sequence after each state-modifying operation.

> **3. Variable-length password.** The specification describes the password as a "fixed-length string," but our implementation uses variable-length strings with a u16 length prefix, identical to the encoding used for the account holder name. This simplifies the marshalling logic (one encoding scheme for all strings) while still supporting fixed-length passwords if the user chooses to always enter the same length. We considered this a reasonable simplification.

> **4. Two currency types.** The `Currency` enum supports SGD (Singapore Dollar) and RM (Malaysian Ringgit). No currency conversion is implemented — transfers between accounts require both accounts to use the same currency type. This keeps the transfer logic simple and avoids the need for exchange rates.

> **5. Minimum monitor interval of 10 seconds.** A minimum interval prevents excessively frequent re-registration requests that would provide no practical benefit. The server rejects monitor requests with an interval shorter than 10 seconds.

> **6. Blocking client during monitoring.** As permitted by the specification, the client is blocked from sending new requests during the monitor interval. The client enters a receive loop and simply waits for callback notifications. This avoids the need for multi-threading on the client side.

> **7. No reply cache eviction.** The at-most-once reply cache grows indefinitely. For the scope of this project (short-lived demonstrations with a limited number of requests), this is acceptable. In a production system, a time-based or LRU eviction policy would be implemented.

> **8. Request ID scope.** Each client process generates request IDs starting from 1 using a monotonically increasing counter. If a client process is restarted, the counter resets to 1, which could potentially collide with cached entries from the previous client session. For this project's scope (short demonstrations), this is not a practical concern. A production system might use UUIDs or include a process start timestamp in the request ID.

> **9. Account ID auto-increment.** Account IDs are assigned starting from 1 and increment by 1 for each new account. Closed account IDs are not reused. The counter is stored in the `ServerApp` instance and resets to 1 when the server is restarted.

**Missing in code:** Nothing. Write directly from the code.

---

## Section 8 — Conclusion (~0.5 page)

**Draft paragraph you can adapt:**

> In this project, we successfully designed and implemented a distributed banking system using a client-server architecture over UDP. The system supports seven banking operations: opening and closing accounts, depositing and withdrawing money, monitoring account updates via a callback mechanism, viewing account information (idempotent), and transferring money between accounts (non-idempotent). All client-server communication uses a custom binary protocol with manually implemented marshalling and unmarshalling, without relying on any existing RMI, RPC, or serialization frameworks.

> We implemented two invocation semantics — at-least-once and at-most-once — to handle the unreliability of UDP. The at-least-once implementation uses client-side retransmission without server-side duplicate detection, which is simple but unsafe for non-idempotent operations. The at-most-once implementation adds a server-side reply cache indexed by a composite request identifier (request ID, client IP, client port), ensuring that duplicate requests are answered from the cache without re-execution.

> Through controlled experiments with simulated packet loss, we demonstrated that at-least-once semantics can lead to incorrect results for non-idempotent operations (e.g., double withdrawal, double transfer), while at-most-once semantics correctly prevent these issues by returning cached replies for retransmitted requests. Both semantics perform correctly for idempotent operations like VIEW_ACCOUNT.

> Key takeaways from this project include: (1) the importance of choosing appropriate invocation semantics based on the idempotency of operations, (2) the trade-offs between implementation simplicity (at-least-once) and correctness guarantees (at-most-once), and (3) practical experience with UDP socket programming, binary protocol design, and application-layer fault tolerance in distributed systems.

**Missing:** Update after running experiments to confirm results match expectations.

---

## CODE COMMENT REQUIREMENTS (before submission)

The lab manual requires "well-commented source code." Current state: **near-zero comments in all files.**

Files that need comments added:
- [ ] `common/protocol.h` — explain each enum and its purpose
- [ ] `common/marshalling.h` — explain ByteWriter/ByteReader purpose, the hton_u64 trick for 64-bit byte swap, the double_to_u64 bit-cast approach
- [ ] `server/models.h` — explain each struct field, why ReqID has three components, why ReqIDHash uses XOR
- [ ] `server/server_app.cpp` — explain the main loop flow, the duplicate detection check, each handler's validation logic
- [ ] `server/monitor_service.cpp` — explain the pruning mechanism, the register-or-refresh logic, the callback broadcast
- [ ] `server/reply_utils.cpp` — explain why the reply header has padding
- [ ] `client/client.cpp` — explain the retry loop structure, timeout handling, the monitor blocking loop, why next_rid is global static

**What to prompt:** "Add detailed comments to all source files. Explain the purpose of each function, the retry/timeout logic, the marshalling approach, the at-most-once deduplication mechanism, and the monitor callback flow. These are needed for the lab submission."

---

## IMPLEMENTATION GAPS SUMMARY — Ready-to-use prompts

### Gap 1 — Message Loss Simulation (BLOCKING for experiments)
**Prompt to use:** "Add a configurable simulated packet drop rate to the client and server. Accept it as the last command-line argument (e.g., `./server_app 2222 0.3` for 30% drop rate, `./client_app 127.0.0.1 2222 0 0.3`). On the client side, before each `sendto()`, generate a random number; if below the drop rate, skip the send and print `[SIMULATED] Request packet dropped`. On the server side, do the same in `send_buffer_to_client()` and print `[SIMULATED] Reply packet dropped`. Use `std::mt19937` seeded with `std::random_device` and `std::uniform_real_distribution<double>(0.0, 1.0)`. Default to 0.0 (no drops) if the argument is not provided."

### Gap 2 — At-Most-Once Client Retries (BLOCKING for experiments)
**Prompt to use:** "In client.cpp, change the retry logic so that `attempts = 3` for BOTH at-least-once and at-most-once semantics. Currently it is `int attempts = semantic == Semantics::AT_LEAST_ONCE ? 3 : 1;`. Change all 6 occurrences to just `int attempts = 3;`. The difference between the two semantics is the SERVER behavior (caching vs no caching), not the client retry count."

### Gap 3 — Server Request/Reply Logging (REQUIRED by spec)
**Prompt to use:** "Add server-side logging to print each incoming request and each outgoing reply. In `ServerApp::run()`, after parsing the header, print: `[REQ] <client_ip>:<client_port> <opcode_name> rid=<rid> semantic=<semantic_name>`. In `send_buffer_to_client()`, print: `[REPLY] rid=<rid> -> <client_ip>:<client_port>`. Use `inet_ntop()` for the IP and `ntohs()` for the port. Create a helper function to convert Opcode to string name."

### Gap 4 — Source Code Comments (REQUIRED by spec)
**Prompt to use:** "Add detailed comments to all source files in the project. For each file, add a file-level comment explaining its purpose. For each function, add a brief comment explaining what it does. For complex logic (byte order conversion, duplicate detection, monitor callback), add inline comments. This is required for submission."
