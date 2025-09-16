import getpass
import os

_inp = input
_pr = print

_clear_cmd = 'cls' if os.name == 'nt' else 'clear'

def clearScreen():
    os.system(_clear_cmd)

def pause():
    _inp("\nPress Enter to continue...")

def getFloat(prompt):
    while 1:
        try:
            return float(_inp(prompt))
        except ValueError:
            _pr("Invalid number.")

class BankAccount:
    def __init__(self, username, password, agency):
        self.username = username
        self.password = password
        self.agency = agency
        self.balance = 0.0
        self.credit_limit = 100.0
        self.credit_used = 0.0
        self.transactions = []
        self.pix_key = None

    def logTransaction(self, msg):
        self.transactions.append(msg)

    def deposit(self, amount):
        clearScreen()
        if amount > 0:
            self.balance += amount
            msg = f"Deposited {amount}. Balance: {self.balance}"
            self.logTransaction(msg)
        else:
            msg = "Invalid deposit amount."
        _pr(msg)

    def withdraw(self, amount):
        clearScreen()
        if amount <= 0:
            _pr("Invalid withdrawal amount.")
        elif amount <= self.balance:
            self.balance -= amount
            msg = f"Withdrew {amount}. Balance: {self.balance}"
            self.logTransaction(msg)
            _pr(msg)
        else:
            _pr("Insufficient funds.")

    def transfer(self, amount, pix_key):
        clearScreen()
        if not self.pix_key:
            _pr("You must set your own PIX key before making transfers.")
            return
        if amount <= 0:
            _pr("Invalid transfer amount.")
            return

        if pix_key == self.pix_key:
            available_credit = self.credit_limit - self.credit_used
            if amount <= available_credit:
                self.credit_used += amount
                self.balance += amount
                msg = f"Added {amount} to balance using credit."
                self.logTransaction(msg)
                _pr(msg)
            else:
                _pr("Not enough credit available.")
        else:
            target = pix_registry.get(pix_key)
            if not target or not target.pix_key:
                _pr("Target PIX key not found or not set.")
                return
            if amount <= self.balance:
                self.balance -= amount
                target.balance += amount
                self.logTransaction(f"Transferred {amount} to {target.username}. Balance: {self.balance}")
                target.logTransaction(f"Received {amount} from {self.username}. Balance: {target.balance}")
                _pr(f"Transferred {amount} to {target.username}.")
            else:
                _pr("Insufficient funds for transfer.")

    def changeCreditLimit(self, new_limit):
        clearScreen()
        if new_limit >= self.credit_used:
            self.credit_limit = new_limit
            msg = f"Credit limit updated to {new_limit}"
            self.logTransaction(msg)
            _pr(msg)
        else:
            _pr("New limit cannot be lower than current credit used.")

    def changePass(self, old, new):
        clearScreen()
        if self.password != old:
            _pr("Incorrect current password.")
            return False
        if not new:
            _pr("New password cannot be empty.")
            return False
        self.password = new
        self.logTransaction("Password changed.")
        _pr("Password updated successfully.")
        return True

accounts = {}
pix_registry = {}

for name in ("user1", "user2"):
    acc = BankAccount(name, "pass", "DF")
    acc.pix_key = name + "pix"
    accounts[name] = acc
    pix_registry[acc.pix_key] = acc

def signUp():
    clearScreen()
    username = _inp("Choose username: ")
    if username in accounts:
        clearScreen()
        _pr("Username already taken.")
        return

    password = getpass.getpass("Enter password: ")
    agency = _inp("Enter agency: ")
    if not agency:
        clearScreen()
        _pr("Agency is required.")
        pause()
        return

    accounts[username] = BankAccount(username, password, agency)
    clearScreen()
    _pr("Account registered successfully!")

def logIn():
    clearScreen()
    username = _inp("Enter username: ")
    password = getpass.getpass("Enter password: ")
    account = accounts.get(username)

    if account and account.password == password:
        clearScreen()
        _pr(f"Welcome, {username}!")
        pause()
        accountMenu(account)
    else:
        clearScreen()
        _pr("Invalid credentials.")

def showAccountInfo(account):
    _pr(f"Username: {account.username}")
    _pr(f"Agency: {account.agency}")
    _pr(f"Balance: {account.balance}")
    _pr(f"Credit Used: {account.credit_used}/{account.credit_limit}")
    _pr(f"PIX Key: {account.pix_key or 'Not set'}")

def accountMenu(account):
    while 1:
        clearScreen()
        _pr("--- Account Menu ---")
        _pr("1. Deposit")
        _pr("2. Withdraw")
        _pr("3. PIX Transfer")
        _pr("4. Show Account Info")
        _pr("5. View Transactions")
        _pr("6. Set/Update PIX Key")
        _pr("7. Change Credit Limit")
        _pr("8. Change Password")
        _pr("9. Logout")

        choice = _inp("Choose option: ")

        match choice:
            case "1":
                amount = getFloat("Deposit amount: ")
                account.deposit(amount)
            case "2":
                amount = getFloat("Withdraw amount: ")
                account.withdraw(amount)
            case "3":
                pix_key = _inp("Target PIX key: ").strip()
                amount = getFloat("Transfer amount: ")
                account.transfer(amount, pix_key)
            case "4":
                clearScreen()
                showAccountInfo(account)
            case "5":
                clearScreen()
                _pr("--- Transaction Log ---")
                _pr(*account.transactions, sep="\n")
            case "6":
                clearScreen()
                pix_key = _inp("Enter new PIX key: ").strip()
                if not pix_key:
                    _pr("PIX key cannot be empty.")
                elif pix_key in pix_registry and pix_registry[pix_key] != account:
                    _pr("This PIX key is already registered.")
                else:
                    if account.pix_key:
                        pix_registry.pop(account.pix_key, None)
                    account.pix_key = pix_key
                    pix_registry[pix_key] = account
                    account.logTransaction("PIX key set/updated.")
                    _pr("PIX key updated successfully.")
            case "7":
                new_limit = getFloat("New credit limit: ")
                account.changeCreditLimit(new_limit)
            case "8":
                old = getpass.getpass("Enter current password: ")
                new = getpass.getpass("Enter new password: ")
                account.changePass(old, new)
            case "9":
                clearScreen()
                _pr("Logged out.")
                break
            case _:
                _pr("Invalid option.")
        pause()

while 1:
    clearScreen()
    _pr("=== Simple Bank App ===")
    _pr("1. Register")
    _pr("2. Login")
    _pr("3. Exit")
    choice = _inp("Choose option: ")

    match choice:
        case "1":
            signUp()
        case "2":
            logIn()
        case "3":
            clearScreen()
            _pr("Goodbye!")
            pause()
            break
        case _:
            _pr("Invalid choice.")
    pause()