# CodeFS
CodeFS is a filesystem for remote software development.

# Why make CodeFS?

Remote development is a key component of software engineering.  Whether developing for embedded devices or building large ML pipelines in the cloud, one often finds oneself needing to work jointly on a local laptop and on a desktop, embedded device, or virtual server.

There are already several approaches for this, here is a breakdown of their pros and cons:

| Tool                          | Pros                    | Cons                               |
| ----------------------------- | ----------------------- | ---------------------------------- |
| sshfs                         | POSIX interface         | Slow, especially to fetch metadata |
| rmate/nuclide                 | Fast, easy to use       | Requires IDE plugins               |
| ssh + console ide (vim/emacs) | Needs no extra software | Lag when editing                   |
| DropBox/syncthing             | Replicates all files    | Replicates all files               |


CodeFS brings the POSIX interface of sshfs with the speed that comes with a dedicated server process.

# Current State

CodeFS is in alpha testing.  You should only use this on directories that are source controlled and you should git push often.

# How to install

## OS/X

Use homebrew:

```
brew cask install osxfuse
brew install --HEAD MisterTea/codefs/codefs
```

## Building from Source

First install the dependencies (either from a package manager or source):

1. Boost
2. Protobuf
3. GFlags
4. ZeroMQ
5. fswatch
6. FUSE (or OSXFUSE for mac) for the client.  Build with -DBUILD_CLIENT=OFF to skip the client if you cannot install FUSE on the server.

Then:

```
git clone https://github.com/MisterTea/CodeFS.git --recurse-submodules
cd CodeFS
mkdir build
cd build
cmake ../
make -j4
make install
```

# User Guide

## Starting the server

Note that, as of now, there is **no** security or encryption.  This means that port 2298 should **not** be exposed to the outside.  Instead, codefs should be run over a secure layer, such as Eternal Terminal: https://github.com/MisterTea/EternalTerminal

To connect to the server with port forwarding:

```
et -x -t="2298:2298" my_server.com
```

Then inside the et/ssh session, run:

```
codefsserver --path=/my/code/path --logtostdout
```

Where ```/my/code/path``` is the location of your code.  For now, the server needs to be restarted every time the client (re)connects.

## Running the client

On the client, run:

```
codefs --path=/tmp/my_development_path --logtostdout
```

Where ```/tmp/my_development_path``` is some empty folder that will act like a mirror to the folder on the server.

# Troubleshooting

### Client doesn't connect to server

In 0.0.1, the server needs to finish indexing the entire directory before the client will be able to connect to it.
