# GitHub Setup & Management Guide for Shobots Controller

This guide explains how to initialize Git, configure `git tree`, create a GitHub repository, push code, and manage branches for **Shobots Controller**.

---

## 1. Local Git Initialization

If Git is not already initialized in your project folder, open PowerShell or Command Prompt in `c:\Users\marki\OneDrive\Desktop\Hexapod` and run:

```bash
git init
```

### Configure `git tree` Command Shortcut
To enable the `git tree` command shortcut:
```bash
git config alias.tree "log --graph --oneline --all --decorate"
```
Now, whenever you run `git tree`, Git will output a colorized graph of all commits and branches!

---

## 2. Creating a GitHub Repository

1. Go to [GitHub](https://github.com) and sign in to your account.
2. Click the **`+`** icon in the top right and select **New repository**.
3. Name your repository: `Shobots` (or `Shobots-Controller`).
4. Choose **Public** or **Private**.
5. Do **NOT** initialize with a README, .gitignore, or license (we already have them in our project).
6. Click **Create repository**.

---

## 3. Pushing Local Code to GitHub

Copy the repository URL provided by GitHub (e.g. `https://github.com/<your-username>/Shobots.git`) and run:

```bash
# Add all files to staging
git add .

# Create initial commit
git commit -m "Initial commit of Shobots Controller"

# Set main branch
git branch -M main

# Add GitHub remote repository
git remote add origin https://github.com/<your-username>/Shobots.git

# Push code to GitHub
git push -u origin main
```

---

## 4. Useful Git Commands for Shobots

| Command | Action |
| :--- | :--- |
| `git tree` | View visual commit history tree graph |
| `git status` | Show changed and untracked files |
| `git add .` | Stage all modified and new files |
| `git commit -m "Message"` | Commit staged changes locally |
| `git push` | Push committed changes to GitHub |
| `git pull` | Fetch and merge changes from GitHub |
| `git checkout -b feature-name` | Create and switch to a new development branch |

---

## 5. Quick Git Tree Batch Launcher

You can also double-click [`git_tree.bat`](file:///c:/Users/marki/OneDrive/Desktop/Hexapod/git_tree.bat) or run `git_tree.bat` from Command Prompt at any time to view your Git commit tree history.
