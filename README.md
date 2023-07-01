 # 42 school exam rank 06 validated

 - TRY to make your own solution for the exam but have the following steps in mind
 - don't forget to use `listen(sockfd, 4096)`
 - you can use `select(FD_SETSIZE, ....)` instead of the `select(maxfd + 1, ...)` technique i saw on many projects
 - use the functions they give to you in the main => `str_join()` and `extract_message()`
 - you can use a `struct` to store informations about your clients
 - when a client quits, for example using CTRL+C you need to send what was remaining in their buffer to the other clients before sending the message about them quiting (if you don't understand pay attention to the part `if (recv_ret == 0)`)

 
 - if you want to test your code using grademe you may need to make the port reusable on process interruption: initialize `int opt = 1` and use `setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))` after `sockfd = socket(....)`
