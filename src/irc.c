/*
 * Copyright (c) 2010 John Gordon <jgor@indiecom.org>
 *   and Doug Farre <dougfarre@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <search.h>
#include "core.h"
#include "sock.h"
#include "irc.h"

int user_fd = 0;

/*
 * Registers newly-connected clients via NICK and USER commands as specified
 * in RFC 1459.
 */
void reg_conn(int cli_sockfd, user_t *user) {
    debug("reg_conn() entered.\n");
    char buf[MAX_BUF + 1], *line, *tok;
    int err, i;

    while (!(user->reg)) {
        memset(&buf, 0, sizeof(buf));
        err = read(cli_sockfd, buf, MAX_BUF);
        if (err == -1) error("read failed.");

        tok = strtok(buf, " ,");

        while (tok != NULL) {
            if (strcmp(tok, "NICK") == 0) {
                tok = strtok(NULL, "\n");
                strncpy(user->nick, tok, MAX_NICK);
                if (user->nick[strlen(user->nick) - 1] == '\r')
                    user->nick[strlen(user->nick) - 1] = '\0';
                debug("user->nick = %s\n", user->nick);
            }

            else if (strcmp(tok, "USER") == 0) {
                tok = strtok(NULL, " ,");
                strncpy(user->user, tok, MAX_USER);
                debug("user->user = %s\n", user->user);
                tok = strtok(NULL, " ,");
                tok = strtok(NULL, " ,");
                tok = strtok(NULL, "\n");
                if (tok[0] == ':') tok++;
                strncpy(user->real, tok, MAX_REAL);
                if (user->real[strlen(user->real) - 1] == '\r')
                    user->real[strlen(user->real) - 1] = '\0';
                debug("user->real = %s\n", user->real);
            }

            tok = strtok(NULL, " ,");
        }

        if (strlen(user->nick) && strlen(user->user) && strlen(user->real))
            user->reg = 1;
    }

    debug("registration successful.\n");

    char preamble[6][MAX_BUF + 1];
    snprintf(preamble[0], MAX_BUF, ":%s %d %s :- %s Message of the Day -\n", SERVER, RPL_MOTDSTART, user->nick, SERVER);
    snprintf(preamble[1], MAX_BUF, ":%s %d %s :- Welcome to ircss. Enjoy!\n", SERVER, RPL_MOTD, user->nick);
    snprintf(preamble[2], MAX_BUF, ":%s %d %s :End of /MOTD command.\n", SERVER, RPL_ENDOFMOTD, user->nick);
    snprintf(preamble[3], MAX_BUF, ":%s!~%s@%s JOIN :%s\n", user->nick, user->user, user->host, CHANNEL);
    snprintf(preamble[4], MAX_BUF, ":%s %d %s %s :%s\n", SERVER, RPL_TOPIC, user->nick, CHANNEL, TOPIC);
    snprintf(preamble[5], MAX_BUF, ":%s %d %s = %s :%s %s\n", SERVER, RPL_NAMREPLY, user->nick, CHANNEL, BOT_NICK, user->nick);

    for (i = 0; i < sizeof(preamble) / MAX_BUF; i++) {
        err = write(cli_sockfd, preamble[i], strlen(preamble[i]));
        if (err == -1) error("write failed.");
    }
}

/*
 * Parses messages from the client.
 */
void cli_read(int cli_sockfd) {
    int err, i;
    char buf[MAX_BUF], cmd[MAX_BUF], msg[MAX_BUF], *tok;

    while (1) {
        memset(&buf, 0, sizeof(buf));
        err = read(cli_sockfd, buf, MAX_BUF);
        if (err == -1) error("read failed.");
        else if (err == 0) break;

        tok = strtok(buf, " ");

        if (strcmp(tok, "PRIVMSG") == 0) {
            debug("got PRIVMSG\n");
            tok = strtok(NULL, " ");
            if (strcmp(tok, CHANNEL) == 0) {
                debug("got %s\n", CHANNEL);
                tok = strtok(NULL, " ");
                if (tok[0] == ':') tok++;
                if (tok[0] == '.') {
                    debug("got .\n");
                    tok++;
                    sscanf(tok, "%s", &cmd);
                    debug("cmd = %s\n", cmd);

                    if (strcmp(cmd, "exit") == 0) {
                        close(cli_sockfd);
                        debug("client exited.\n");
                        exit(EXIT_SUCCESS);
                    }

                    else if (strcmp(cmd, "cmd") == 0) {
                        tok = strtok(NULL, "\r\n");
                        snprintf(msg, MAX_BUF, "CMD %s\n", tok);

                        for (i = 1; i <= bot_fd; i++) {
                            char search[MAX_BUF];
                            ENTRY search_item;
                            ENTRY *result_item;

                            snprintf(search, MAX_BUF, "bot%d", i);

                            search_item.key = search;

                            debug("searching for %s\n", search_item.key);

                            result_item = hsearch(search_item, FIND);

                            if (result_item != NULL)
                                ss_write(((bot_t *)result_item->data)->sockfd, msg);
                            else debug("socket not found.\n");
                        }

                    }
                    
                    else {
                        debug("unknown command: %s\n", cmd);  
                        continue;
                    }
                }
                else cli_write(cli_sockfd, tok);
            }
        }
    }
}

/*
 * Sends a message to the client.
 */
void cli_write(int cli_sockfd, char *msg) {
    int err;
    char buf[MAX_BUF];

    debug("cli_write() entered, cli_sockfd = %d\n", cli_sockfd);
    debug("msg = %s\n", msg);

    snprintf(buf, MAX_BUF, ":%s!~%s@%s PRIVMSG %s :%s", BOT_NICK, BOT_USER, BOT_HOST, CHANNEL, msg);

    err = write(cli_sockfd, buf, strlen(buf));
    if (err == -1) error("write failed.");
}

/*
 * Starts a thread to listen for user input on given sockfd.
 */
void *run_irc_cli(void *ptr) {
    int *cli_sockfd_ptr = (int *) ptr;
    int cli_sockfd = *cli_sockfd_ptr;
    user_t user;
    char s[INET6_ADDRSTRLEN];
    char host[MAX_HOST + 1];
    struct sockaddr_storage cli_addr;
    int cli_len = sizeof(cli_addr);

    user.reg = 0;
    user.nick = calloc(MAX_NICK + 1, sizeof(char));
    user.user = calloc(MAX_USER + 1, sizeof(char));
    user.real = calloc(MAX_REAL + 1, sizeof(char));
    user.host = calloc(MAX_HOST + 1, sizeof(char));

    getpeername(cli_sockfd, (struct sockaddr *) &cli_addr, &cli_len);
    inet_ntop(cli_addr.ss_family, get_in_addr((struct sockaddr *)&cli_addr), s, sizeof(s));
    getnameinfo((struct sockaddr *)&cli_addr, sizeof(cli_addr), host, sizeof(host), NULL, 0, 0);
    strncpy(user.host, host, MAX_HOST);
    debug("user.host = %s\n", user.host);

    reg_conn(cli_sockfd, &user);

    cli_read(cli_sockfd);
}

/*
 * Starts a listening irc server on the given port.
 */
void *run_irc_srv(void *ptr) {
    int *port_ptr = (int *) ptr;
    int port = *port_ptr;
    int srv_sockfd, cli_sockfd;
    pthread_t pt_read;

    //hcreate(MAX_BOTS);

    srv_sockfd = get_srv_sock(port);

    while (1) {
        cli_sockfd = get_cli_sock(srv_sockfd);
        user_fd++;

        char key[MAX_BUF];
        user_t data;
        ENTRY item;

        sprintf(key, "user%d", user_fd);

        data.sockfd = cli_sockfd;
        item.key = key;
        item.data = &data;

        debug("entering %s -> sockfd=%d\n", item.key, ((user_t *)item.data)->sockfd);

        hsearch(item, ENTER);

        pthread_create(&pt_read, NULL, run_irc_cli, (void *) &cli_sockfd);
    }
}

