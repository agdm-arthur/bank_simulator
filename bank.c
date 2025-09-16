// bank.c - Optimized Simple Bank App
// - cents-based money
// - ring-buffer tx log
// - open-addressing PIX hash table
// - ANSI clear (VT on Windows)
// Compile: gcc -O3 -march=native -std=gnu11 -flto -pipe -Wall -Wextra -o bank.exe bank.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <ctype.h>

#if defined(_WIN32) || defined(_WIN64)
  #include <conio.h>
  #include <windows.h>
#else
  #include <termios.h>
  #include <unistd.h>
#endif

/* ---------- Config ---------- */
#define MAX_ACCOUNTS   1024
#define MAX_NAME_LEN   32
#define MAX_PASS_LEN   64
#define MAX_AGENCY_LEN 8
#define MAX_PIX_LEN    64

#define TX_LOG_SIZE    256
#define TX_NOTE_LEN    40

/* PIX hash table size (power of two) */
#define PIX_TABLE_BITS 11
#define PIX_TABLE_SIZE (1u << PIX_TABLE_BITS)  // 2048

/* ---------- Types & Macros ---------- */
typedef int64_t cents_t; // money in cents

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* transaction type */
typedef enum { TX_DEPOSIT=1, TX_WITHDRAW=2, TX_TRANSFER_OUT=3, TX_TRANSFER_IN=4, TX_CREDIT_ADV=5, TX_MISC=6 } tx_type_t;

typedef struct {
    tx_type_t type;
    cents_t amount;
    char note[TX_NOTE_LEN];
} tx_t;

/* ring buffer transaction log */
typedef struct {
    tx_t entries[TX_LOG_SIZE];
    unsigned head;   // index of oldest
    unsigned count;  // number stored
} txlog_t;

/* Account */
typedef struct {
    char username[MAX_NAME_LEN];
    char password[MAX_PASS_LEN];
    char agency[MAX_AGENCY_LEN];
    cents_t balance;       // cents
    cents_t credit_limit;  // cents
    cents_t credit_used;   // cents
    char pix_key[MAX_PIX_LEN];

    txlog_t txlog;

    int used; // boolean
} Account;

/* ---------- Globals ---------- */
static Account accounts[MAX_ACCOUNTS];
static unsigned account_count = 0;

/* PIX table stores account_index+1 (0 == empty) */
static int pix_table[PIX_TABLE_SIZE];

/* ---------- Utilities ---------- */
static inline void enable_vt_on_windows(void) {
#if defined(_WIN32) || defined(_WIN64)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);
#endif
}

/* fast clear using ANSI */
static inline void fast_clear(void) {
    fputs("\x1b[2J\x1b[H", stdout);
    fflush(stdout);
}

/* safe string copy using memcpy after computing length */
static inline void safe_strcpy(char *dst, const char *src, size_t dstcap) {
    if (dstcap == 0) return;
    size_t n = strnlen(src, dstcap-1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* read line into buf */
static inline void readln(char *buf, size_t cap) {
    if (!fgets(buf, (int)cap, stdin)) {
        buf[0] = '\0';
        return;
    }
    size_t len = strlen(buf);
    if (len && buf[len-1] == '\n') buf[len-1] = '\0';
}

/* small password input (no-echo) */
static void get_password(const char *prompt, char *out, size_t cap) {
    if (prompt) { fputs(prompt, stdout); fflush(stdout); }
#if defined(_WIN32) || defined(_WIN64)
    size_t i = 0;
    int ch;
    while ((ch = _getch()) != '\r' && ch != '\n' && ch != EOF && i + 1 < cap) {
        if (ch == '\b') { if (i) i--; continue; }
        out[i++] = (char)ch;
    }
    out[i] = '\0';
    putchar('\n');
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    if (fgets(out, (int)cap, stdin) == NULL) out[0]=0;
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    size_t l = strlen(out);
    if (l && out[l-1]=='\n') out[l-1]=0;
    putchar('\n');
#endif
}

/* ---------- Money parsing ---------- */
/* parse string like "123.45" or "12" or "0.5" into cents (int64) */
/* returns 1 on success, 0 on failure */
static int parse_money_to_cents(const char *s, cents_t *out) {
    if (unlikely(!s || !*s)) return 0;
    const char *p = s;
    int sign = 1;
    if (*p == '+') p++;
    else if (*p == '-') { sign = -1; p++; }

    // whole part
    int64_t whole = 0;
    int digits = 0;
    while (*p && *p >= '0' && *p <= '9') {
        whole = whole * 10 + (*p - '0');
        p++; digits++;
        // prevent overflow (very large numbers not expected)
        if (unlikely(whole > (INT64_C(1) << 60))) return 0;
    }

    int64_t frac = 0;
    int fdigits = 0;
    if (*p == '.') {
        p++;
        while (*p && *p >= '0' && *p <= '9' && fdigits < 2) {
            frac = frac * 10 + (*p - '0');
            p++; fdigits++;
        }
        // if only one fractional digit, scale
        if (fdigits == 1) frac *= 10;
        // skip additional fractional digits (no rounding)
        while (*p && *p >= '0' && *p <= '9') p++;
    } else {
        frac = 0;
    }

    // allow trailing whitespace
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '\0') return 0;

    *out = (cents_t)sign * (cents_t)(whole * 100 + frac);
    return 1;
}

/* prompt and read cents (re-prompts on invalid) */
static cents_t read_cents_prompt(const char *prompt) {
    char buf[128];
    cents_t val;
    for (;;) {
        if (prompt) { fputs(prompt, stdout); fflush(stdout); }
        readln(buf, sizeof buf);
        if (parse_money_to_cents(buf, &val)) return val;
        puts("Invalid number.");
    }
}

/* format cents to string like "123.45" into out (outcap >= 32 recommended) */
static void cents_to_str(cents_t c, char *out, size_t outcap) {
    if (outcap == 0) return;
    int neg = 0;
    if (c < 0) { neg = 1; c = -c; }
    int64_t whole = c / 100;
    int64_t frac = c % 100;
    if (neg) snprintf(out, outcap, "-%" PRId64 ".%02" PRId64, whole, frac);
    else snprintf(out, outcap, "%" PRId64 ".%02" PRId64, whole, frac);
}

/* ---------- Transaction log (ring buffer) ---------- */
static inline void txlog_push(txlog_t *log, tx_type_t type, cents_t amount, const char *note) {
    unsigned idx;
    if (log->count < TX_LOG_SIZE) {
        idx = (log->head + log->count) % TX_LOG_SIZE;
        log->count++;
    } else {
        idx = log->head;
        log->head = (log->head + 1) % TX_LOG_SIZE;
    }
    log->entries[idx].type = type;
    log->entries[idx].amount = amount;
    safe_strcpy(log->entries[idx].note, note ? note : "", TX_NOTE_LEN);
}

/* print transaction log */
static void print_txlog(const txlog_t *log) {
    if (log->count == 0) { puts("<no transactions>"); return; }
    for (unsigned i = 0; i < log->count; ++i) {
        unsigned idx = (log->head + i) % TX_LOG_SIZE;
        const tx_t *t = &log->entries[idx];
        char amtbuf[32];
        cents_to_str(t->amount, amtbuf, sizeof amtbuf);
        const char *typ = "MISC";
        switch (t->type) {
            case TX_DEPOSIT: typ = "DEPOSIT"; break;
            case TX_WITHDRAW: typ = "WITHDRAW"; break;
            case TX_TRANSFER_OUT: typ = "XFER_OUT"; break;
            case TX_TRANSFER_IN: typ = "XFER_IN"; break;
            case TX_CREDIT_ADV: typ = "CREDIT"; break;
            default: break;
        }
        printf("[%s] %s  %s\n", typ, amtbuf, t->note);
    }
}

/* ---------- PIX hash table (open addressing, simple FNV-1a) ---------- */
static inline uint64_t fnv1a_hash(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    while (*s) {
        h ^= (unsigned char)(*s++);
        h *= 1099511628211ULL;
    }
    return h;
}

/* find account index by pix key, or -1 */
static int pix_find(const char *key) {
    if (!key || !key[0]) return -1;
    uint64_t h = fnv1a_hash(key);
    uint32_t idx = (uint32_t)h & (PIX_TABLE_SIZE - 1);
    for (;;) {
        int v = pix_table[idx];
        if (v == 0) return -1;
        int ai = v - 1;
        if (ai >= 0 && ai < (int)account_count && accounts[ai].used && accounts[ai].pix_key[0] && strcmp(accounts[ai].pix_key, key) == 0) {
            return ai;
        }
        idx = (idx + 1) & (PIX_TABLE_SIZE - 1);
    }
}

/* insert mapping key -> account_index (overwrites existing if present) */
static void pix_insert(const char *key, int account_index) {
    if (!key || !key[0]) return;
    uint64_t h = fnv1a_hash(key);
    uint32_t idx = (uint32_t)h & (PIX_TABLE_SIZE - 1);
    while (pix_table[idx] != 0) {
        int ai = pix_table[idx] - 1;
        if (ai >= 0 && ai < (int)account_count && accounts[ai].used && strcmp(accounts[ai].pix_key, key) == 0) {
            pix_table[idx] = account_index + 1;
            return;
        }
        idx = (idx + 1) & (PIX_TABLE_SIZE - 1);
    }
    pix_table[idx] = account_index + 1;
}

/* remove mapping for key (if any) keeping clustering intact */
static void pix_remove(const char *key) {
    if (!key || !key[0]) return;
    uint64_t h = fnv1a_hash(key);
    uint32_t idx = (uint32_t)h & (PIX_TABLE_SIZE - 1);
    for (;;) {
        int v = pix_table[idx];
        if (v == 0) return; // not found
        int ai = v - 1;
        if (ai >= 0 && ai < (int)account_count && accounts[ai].used && accounts[ai].pix_key[0] && strcmp(accounts[ai].pix_key, key) == 0) {
            // remove and rehash following cluster
            pix_table[idx] = 0;
            uint32_t j = (idx + 1) & (PIX_TABLE_SIZE - 1);
            while (pix_table[j] != 0) {
                int re_ai = pix_table[j] - 1;
                pix_table[j] = 0;
                if (re_ai >= 0 && re_ai < (int)account_count && accounts[re_ai].used && accounts[re_ai].pix_key[0]) {
                    pix_insert(accounts[re_ai].pix_key, re_ai);
                }
                j = (j + 1) & (PIX_TABLE_SIZE - 1);
            }
            return;
        }
        idx = (idx + 1) & (PIX_TABLE_SIZE - 1);
    }
}

/* ---------- Account helpers ---------- */
static int find_account_by_name(const char *name) {
    for (unsigned i = 0; i < account_count; ++i) {
        if (accounts[i].used && strcmp(accounts[i].username, name) == 0) return (int)i;
    }
    return -1;
}

static void log_account_tx(Account *a, tx_type_t type, cents_t amt, const char *note) {
    txlog_push(&a->txlog, type, amt, note);
}

/* ---------- Banking operations ---------- */
static void deposit(Account *a, cents_t amount) {
    fast_clear();
    if (unlikely(amount <= 0)) {
        puts("Invalid deposit amount.");
        return;
    }
    a->balance += amount;
    char nb[64];
    cents_to_str(amount, nb, sizeof nb);
    char note[TX_NOTE_LEN];
    snprintf(note, sizeof note, "Deposited %s", nb);
    log_account_tx(a, TX_DEPOSIT, amount, note);
    char bal[64]; cents_to_str(a->balance, bal, sizeof bal);
    printf("Deposited %s. Balance: %s\n", nb, bal);
}

static void withdraw_(Account *a, cents_t amount) {
    fast_clear();
    if (unlikely(amount <= 0)) { puts("Invalid withdrawal amount."); return; }
    if (amount <= a->balance) {
        a->balance -= amount;
        char nb[64]; cents_to_str(amount, nb, sizeof nb);
        char note[TX_NOTE_LEN]; snprintf(note, sizeof note, "Withdrew %s", nb);
        log_account_tx(a, TX_WITHDRAW, amount, note);
        char bal[64]; cents_to_str(a->balance, bal, sizeof bal);
        printf("Withdrew %s. Balance: %s\n", nb, bal);
    } else {
        puts("Insufficient funds.");
    }
}

/* transfer behavior:
   - if target pix == own pix -> credit advance (if credit available)
   - else find target account by pix and transfer if balance sufficient
*/
static void transfer_(Account *a, cents_t amount, const char *pix_key) {
    fast_clear();
    if (unlikely(a->pix_key[0] == '\0')) { puts("You must set your own PIX key before making transfers."); return; }
    if (unlikely(amount <= 0)) { puts("Invalid transfer amount."); return; }

    if (strcmp(pix_key, a->pix_key) == 0) {
        cents_t available = a->credit_limit - a->credit_used;
        if (amount <= available) {
            a->credit_used += amount;
            a->balance += amount;
            char nb[64]; cents_to_str(amount, nb, sizeof nb);
            char note[TX_NOTE_LEN]; snprintf(note, sizeof note, "Credit advance %s", nb);
            log_account_tx(a, TX_CREDIT_ADV, amount, note);
            printf("Added %s to balance using credit.\n", nb);
        } else {
            puts("Not enough credit available.");
        }
        return;
    }

    int tidx = pix_find(pix_key);
    if (tidx < 0) { puts("Target PIX key not found or not set."); return; }
    Account *target = &accounts[tidx];
    if (amount <= a->balance) {
        a->balance -= amount;
        target->balance += amount;
        char amtbuf[64]; cents_to_str(amount, amtbuf, sizeof amtbuf);
        char note_out[TX_NOTE_LEN]; snprintf(note_out, sizeof note_out, "Transferred %s to %s", amtbuf, target->username);
        char note_in[TX_NOTE_LEN]; snprintf(note_in, sizeof note_in, "Received %s from %s", amtbuf, a->username);
        log_account_tx(a, TX_TRANSFER_OUT, amount, note_out);
        log_account_tx(target, TX_TRANSFER_IN, amount, note_in);
        printf("Transferred %s to %s.\n", amtbuf, target->username);
    } else {
        puts("Insufficient funds for transfer.");
    }
}

static void change_credit_limit(Account *a, cents_t new_limit) {
    fast_clear();
    if (new_limit >= a->credit_used) {
        a->credit_limit = new_limit;
        char nb[64]; cents_to_str(new_limit, nb, sizeof nb);
        char note[TX_NOTE_LEN]; snprintf(note, sizeof note, "Credit limit set to %s", nb);
        log_account_tx(a, TX_MISC, 0, note);
        printf("Credit limit updated to %s\n", nb);
    } else {
        puts("New limit cannot be lower than current credit used.");
    }
}

static int change_password(Account *a, const char *oldp, const char *newp) {
    fast_clear();
    if (!oldp || !newp) { puts("Invalid password."); return 0; }
    if (strcmp(a->password, oldp) != 0) { puts("Incorrect current password."); return 0; }
    if (newp[0] == '\0') { puts("New password cannot be empty."); return 0; }
    safe_strcpy(a->password, newp, MAX_PASS_LEN);
    log_account_tx(a, TX_MISC, 0, "Password changed");
    puts("Password updated successfully.");
    return 1;
}

/* ---------- UI & Menus ---------- */
static void pause_screen(void) {
    char tmp[8];
    puts("\nPress Enter to continue...");
    readln(tmp, sizeof tmp);
}

static void show_account_info(const Account *a) {
    char bal[64], cred[64];
    cents_to_str(a->balance, bal, sizeof bal);
    cents_to_str(a->credit_used, cred, sizeof cred);
    printf("Username: %s\n", a->username);
    printf("Agency: %s\n", a->agency);
    printf("Balance: %s\n", bal);
    printf("Credit Used: %s / ", cred);
    cents_to_str(a->credit_limit, bal, sizeof bal);
    printf("%s\n", bal);
    printf("PIX Key: %s\n", a->pix_key[0] ? a->pix_key : "Not set");
}

/* account menu loop */
static void account_menu(Account *account) {
    char choice[64];
    for (;;) {
        fast_clear();
        puts("--- Account Menu ---");
        puts("1. Deposit");
        puts("2. Withdraw");
        puts("3. PIX Transfer");
        puts("4. Show Account Info");
        puts("5. View Transactions");
        puts("6. Set/Update PIX Key");
        puts("7. Change Credit Limit");
        puts("8. Change Password");
        puts("9. Logout");
        fputs("Choose option: ", stdout); fflush(stdout);
        readln(choice, sizeof choice);

        switch (choice[0]) {
            case '1': {
                cents_t amt = read_cents_prompt("Deposit amount: ");
                deposit(account, amt);
                break;
            }
            case '2': {
                cents_t amt = read_cents_prompt("Withdraw amount: ");
                withdraw_(account, amt);
                break;
            }
            case '3': {
                char pix[MAX_PIX_LEN];
                fputs("Target PIX key: ", stdout); fflush(stdout);
                readln(pix, sizeof pix);
                cents_t amt = read_cents_prompt("Transfer amount: ");
                transfer_(account, amt, pix);
                break;
            }
            case '4':
                fast_clear();
                show_account_info(account);
                break;
            case '5':
                fast_clear();
                puts("--- Transaction Log ---");
                print_txlog(&account->txlog);
                break;
            case '6': {
                fast_clear();
                char pix[MAX_PIX_LEN];
                fputs("Enter new PIX key: ", stdout); fflush(stdout);
                readln(pix, sizeof pix);
                if (pix[0] == '\0') {
                    puts("PIX key cannot be empty.");
                } else {
                    int existing = pix_find(pix);
                    int my_index = (int)(account - accounts);
                    if (existing >= 0 && existing != my_index) {
                        puts("This PIX key is already registered.");
                    } else {
                        if (account->pix_key[0]) {
                            pix_remove(account->pix_key);
                            account->pix_key[0] = '\0';
                        }
                        safe_strcpy(account->pix_key, pix, MAX_PIX_LEN);
                        pix_insert(account->pix_key, my_index);
                        log_account_tx(account, TX_MISC, 0, "PIX key set/updated");
                        puts("PIX key updated successfully.");
                    }
                }
                break;
            }
            case '7': {
                cents_t nl = read_cents_prompt("New credit limit: ");
                change_credit_limit(account, nl);
                break;
            }
            case '8': {
                char oldp[MAX_PASS_LEN], newp[MAX_PASS_LEN];
                get_password("Enter current password: ", oldp, sizeof oldp);
                get_password("Enter new password: ", newp, sizeof newp);
                change_password(account, oldp, newp);
                break;
            }
            case '9':
                fast_clear();
                puts("Logged out.");
                return;
            default:
                puts("Invalid option.");
        }
        pause_screen();
    }
}

/* ---------- Sign up / Log in ---------- */
static void sign_up(void) {
    fast_clear();
    char username[MAX_NAME_LEN], agency[MAX_AGENCY_LEN];
    fputs("Choose username: ", stdout); fflush(stdout);
    readln(username, sizeof username);
    if (username[0] == '\0') { puts("Username cannot be empty."); pause_screen(); return; }
    if (find_account_by_name(username) >= 0) { fast_clear(); puts("Username already taken."); pause_screen(); return; }
    char password[MAX_PASS_LEN];
    get_password("Enter password: ", password, sizeof password);
    fputs("Enter agency: ", stdout); fflush(stdout);
    readln(agency, sizeof agency);
    if (agency[0] == '\0') { fast_clear(); puts("Agency is required."); pause_screen(); return; }
    if (account_count >= MAX_ACCOUNTS) { puts("Account limit reached."); pause_screen(); return; }

    Account *a = &accounts[account_count];
    memset(a, 0, sizeof *a);
    a->used = 1;
    safe_strcpy(a->username, username, MAX_NAME_LEN);
    safe_strcpy(a->password, password, MAX_PASS_LEN);
    safe_strcpy(a->agency, agency, MAX_AGENCY_LEN);
    a->balance = 0;
    a->credit_limit = 100 * 100; // 100.00 -> cents
    a->credit_used = 0;
    a->txlog.head = 0; a->txlog.count = 0;
    account_count++;
    fast_clear();
    puts("Account registered successfully!");
}

static void log_in(void) {
    fast_clear();
    char username[MAX_NAME_LEN];
    fputs("Enter username: ", stdout); fflush(stdout);
    readln(username, sizeof username);
    char password[MAX_PASS_LEN];
    get_password("Enter password: ", password, sizeof password);
    int idx = find_account_by_name(username);
    if (idx >= 0 && strcmp(accounts[idx].password, password) == 0) {
        fast_clear();
        printf("Welcome, %s!\n", username);
        pause_screen();
        account_menu(&accounts[idx]);
    } else {
        fast_clear();
        puts("Invalid credentials.");
    }
}

/* ---------- Initialization ---------- */
static void init_sample_accounts(void) {
    // user1
    if (account_count + 2 > MAX_ACCOUNTS) return;
    Account *a = &accounts[account_count++];
    memset(a, 0, sizeof *a);
    a->used = 1;
    safe_strcpy(a->username, "user1", MAX_NAME_LEN);
    safe_strcpy(a->password, "pass", MAX_PASS_LEN);
    safe_strcpy(a->agency, "DF", MAX_AGENCY_LEN);
    safe_strcpy(a->pix_key, "user1pix", MAX_PIX_LEN);
    a->credit_limit = 100 * 100;
    a->balance = 0;
    pix_insert(a->pix_key, (int)(a - accounts));

    // user2
    a = &accounts[account_count++];
    memset(a, 0, sizeof *a);
    a->used = 1;
    safe_strcpy(a->username, "user2", MAX_NAME_LEN);
    safe_strcpy(a->password, "pass", MAX_PASS_LEN);
    safe_strcpy(a->agency, "DF", MAX_AGENCY_LEN);
    safe_strcpy(a->pix_key, "user2pix", MAX_PIX_LEN);
    a->credit_limit = 100 * 100;
    a->balance = 0;
    pix_insert(a->pix_key, (int)(a - accounts));
}

/* ---------- Main ---------- */
int main(void) {
    enable_vt_on_windows();
    memset(pix_table, 0, sizeof pix_table);
    init_sample_accounts();

    char opt[64];
    for (;;) {
        fast_clear();
        puts("=== Simple Bank App ===");
        puts("1. Register");
        puts("2. Login");
        puts("3. Exit");
        fputs("Choose option: ", stdout); fflush(stdout);
        readln(opt, sizeof opt);

        switch (opt[0]) {
            case '1': sign_up(); break;
            case '2': log_in(); break;
            case '3':
                fast_clear();
                puts("Goodbye!");
                pause_screen();
                return 0;
            default:
                puts("Invalid choice.");
                break;
        }
        pause_screen();
    }
    return 0;
}
