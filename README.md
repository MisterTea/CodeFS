# CodeFS
CodeFS is a filesystem for remote software development.

# Why make CodeFS?

Remote development is a key component of software engineering.  Whether developing for embedding devices or building large ML pipelines in the cloud, one often finds oneself needing to work jointly on a local laptop and on a desktop, embedded device, or virtual server.

There are already several approaches for this, here is a breakdown of their pros and cons:

| Tool                          | Pros                    | Cons                               |
| ----------------------------- | ----------------------- | ---------------------------------- |
| sshfs                         | POSIX interface         | Slow, especially to fetch metadata |
| rmate/nuclide                 | Fast, easy to use       | Requires IDE plugins               |
| ssh + console ide (vim/emacs) | Needs no extra software | Lag when editing                   |

CodeFS brings the POSIX interface of sshfs with the speed that comes with a dedicated server.

# Current State

CodeFS is in alpha testing.  You should only use this on directories that are source controlled and you should git push often.


