import getpass
import os

def getFloat(prompt):
    while True:
        try:
            return float(input(prompt))
        except ValueError:
            print("Invalid number.")

def clearScreen():
    os.system('cls' if os.name == 'nt' else 'clear')

def pause():
    input("\nPress Enter to continue...")

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

    def logTransaction(self, description):
        self.transactions.append(description)

    def deposit(self, amount):
        if amount > 0:
            self.balance += amount
            self.logTransaction(f"Deposited {amount}. Balance: {self.balance}")
            clearScreen()
            print(f"Deposited {amount}. New balance: {self.balance}")
        else:
            clearScreen()
            print("Invalid deposit amount.")

    def withdraw(self, amount):
        if amount <= 0:
            clearScreen()
            print("Invalid withdrawal amount.")
            return
        if amount <= self.balance:
            self.balance -= amount
            self.logTransaction(f"Withdrew {amount}. Balance: {self.balance}")
            clearScreen()
            print(f"Withdrew {amount}. New balance: {self.balance}")
        else:
            clearScreen()
            print("Insufficient funds.")

    def transfer(self, amount, pix_key):
        if not self.pix_key:
            clearScreen()
            print("You must set your own PIX key before making transfers.")
            return
        if amount <= 0:
            clearScreen()
            print("Invalid transfer amount.")
            return
        if pix_key == self.pix_key:
            if amount <= self.credit_limit - self.credit_used:
                self.credit_used += amount
                self.balance += amount
                self.logTransaction(f"Added {amount} to balance using credit.")
                clearScreen()
                print(f"Added {amount} to balance using credit.")
            else:
                clearScreen()
                print("Not enough credit available.")
        else:
            target_account = pix_registry.get(pix_key)
            if not target_account:
                clearScreen()
                print("Target PIX key not found.")
                return
            if not target_account.pix_key:
                clearScreen()
                print("Target account has not set a PIX key.")
                return
            if amount <= self.balance:
                self.balance -= amount
                target_account.balance += amount
                self.logTransaction(f"Transferred {amount} to {target_account.username} (PIX). Balance: {self.balance}")
                target_account.logTransaction(f"Received {amount} from {self.username} (PIX). Balance: {target_account.balance}")
                clearScreen()
                print(f"Transferred {amount} to {target_account.username}.")
            else:
                clearScreen()
                print("Insufficient funds for transfer.")

    def changeCreditLimit(self, new_limit):
        if new_limit >= self.credit_used:    # cannot set lower than already used
            self.credit_limit = new_limit
            self.logTransaction(f"Changed credit limit to {new_limit}")
            clearScreen()
            print(f"Credit limit updated to {new_limit}")
        else:
            clearScreen()
            print("New limit cannot be lower than current credit used.")

    def changePass(self, old_password, new_password):
        if self.password != old_password:
            clearScreen()
            print("Incorrect current password.")
            return False
        if not new_password:
            clearScreen()
            print("New password cannot be empty.")
            return False
        self.password = new_password
        self.logTransaction("Password changed.")
        clearScreen()
        print("Password updated successfully.")
        return True

accounts = {}
pix_registry = {}

default_user1 = BankAccount("user1", "pass", "DF")
default_user1.pix_key = "user1pix"
accounts["user1"] = default_user1
pix_registry["user1pix"] = default_user1

default_user2 = BankAccount("user2", "pass", "DF")
default_user2.pix_key = "user2pix"
accounts["user2"] = default_user2
pix_registry["user2pix"] = default_user2

def signUp():
    clearScreen()
    username = input("Choose username: ")
    if username in accounts:
        clearScreen()
        print("Username already taken.")
        return
    password = getpass.getpass("Enter password: ")
    agency = input("Enter agency: ")
    if not agency.strip():
        clearScreen()
        print("Agency is required.")
        pause()
        return
    accounts[username] = BankAccount(username, password, agency)
    clearScreen()
    print("Account registered successfully!")

def logIn():
    clearScreen()
    username = input("Enter username: ")
    password = getpass.getpass("Enter password: ")
    account = accounts.get(username)
    if account and account.password == password:
        clearScreen()
        print(f"Welcome, {username}!")
        pause()
        accountMenu(account)
    else:
        clearScreen()
        print("Invalid credentials.")

def accountMenu(account):
    while True:
        clearScreen()
        print("--- Account Menu ---")
        print("1. Deposit")
        print("2. Withdraw")
        print("3. PIX Transfer")
        print("4. Show Account Info")
        print("5. View Transactions")
        print("6. Set/Update PIX Key")
        print("7. Change Credit Limit")
        print("8. Change Password")
        print("9. Logout")

        choice = input("Choose option: ")
        if choice == "1":
            clearScreen()
            amount = getFloat("Deposit amount: ")
            account.deposit(amount)
            pause()
        elif choice == "2":
            clearScreen()
            amount = getFloat("Withdraw amount: ")
            account.withdraw(amount)
            pause()
        elif choice == "3":
            clearScreen()
            pix_key = input("Target PIX key: ").strip()
            amount = getFloat("Transfer amount: ")
            account.transfer(amount, pix_key)
            pause()
        elif choice == "4":
            clearScreen()
            print(f"Username: {account.username}")
            print(f"Agency: {account.agency}")
            print(f"Balance: {account.balance}")
            print(f"Credit Used: {account.credit_used}/{account.credit_limit}")
            print(f"PIX Key: {account.pix_key or 'Not set'}")
            pause()
        elif choice == "5":
            clearScreen()
            print("--- Transaction Log ---")
            for t in account.transactions:
                print(t)
            pause()
        elif choice == "6":
            clearScreen()
            pix_key = input("Enter new PIX key: ").strip()
            if not pix_key:
                clearScreen()
                print("PIX key cannot be empty.")
            elif pix_key in pix_registry and pix_registry[pix_key] != account:
                clearScreen()
                print("This PIX key is already registered to another account.")
            else:
                if account.pix_key:
                    pix_registry.pop(account.pix_key, None)
                account.pix_key = pix_key
                pix_registry[pix_key] = account
                account.logTransaction("PIX key set/updated.")
                clearScreen()
                print("PIX key updated successfully.")
            pause()
        elif choice == "7":
            clearScreen()
            new_limit = getFloat("New credit limit: ")
            account.changeCreditLimit(new_limit)
            pause()
        elif choice == "8":
            clearScreen()
            old_pass = getpass.getpass("Enter current password: ")
            new_pass = getpass.getpass("Enter new password: ")
            account.changePass(old_pass, new_pass)
            pause()
        elif choice == "9":
            clearScreen()
            print("Logged out.")
            break
        else:
            clearScreen()
            print("Invalid option.")
            pause()

while True:
    clearScreen()
    print("=== Simple Bank App ===")
    print("1. Register")
    print("2. Login")
    print("3. Exit")
    choice = input("Choose option: ")
    if choice == "1":
        signUp()
        pause()
    elif choice == "2":
        logIn()
        pause()
    elif choice == "3":
        clearScreen()
        print("Goodbye!")
        pause()
        break
    else:
        clearScreen()
        print("Invalid choice.")
        pause()