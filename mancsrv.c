#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXNAME 80  /* maximum permitted name size, not including \0 */
#define NPITS 6  /* number of pits on a side, not including the end pit */
#define NPEBBLES 4 /* initial number of pebbles per pit */
#define MAXMESSAGE (MAXNAME + 50) /* initial number of pebbles per pit */

int port = 3000;
int listenfd;

struct player {
    int fd;
    char name[MAXNAME+1];
    int pits[NPITS+1];  // pits[0..NPITS-1] are the regular pits
                        // pits[NPITS] is the end pit
    //other stuff undoubtedly needed here
    struct player *prev;
    struct player *next;
};
struct player *playerlist = NULL;
struct player *whose_turn = NULL;

extern void parseargs(int argc, char **argv);
extern void makelistener();
extern int compute_average_pebbles();
extern int game_is_over();  /* boolean */
extern void broadcast(char *s);  /* you need to write this one */
extern int read_name(struct player *p);
extern void game_display(struct player *exclude);
extern int game_empty ();
extern struct player *last_player ();
extern void remove_player (struct player *p);
extern int read_digit(int fd, struct player *p, fd_set *all_fds);
extern void next_turn ();
extern void move_to_front (struct player *p);
extern void exclusive_broadcast(char *s, struct player *exclude);
extern struct player *next_player (struct player *p);
extern int play (int index);

int main(int argc, char **argv) {
    char msg[MAXMESSAGE];

    parseargs(argc, argv);
    makelistener();

    int max_fd = listenfd;
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(listenfd, &all_fds);

    while (!game_is_over()) {
        fd_set listen_fds = all_fds;
        int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
        if (nready == -1) {
            perror("server: select");
            exit(1);
        }

        // check if it's the original socket
        if (FD_ISSET(listenfd, &listen_fds)) {
            int client_fd = accept (listenfd, NULL, NULL);
            if (client_fd == -1){
                perror("accept");
                close(listenfd);
                exit(1);
            }
            char *greeting = "Welcome to Mancala. What is your name?\r\n";
            if (write(client_fd, greeting, strlen(greeting)) < 0){
                perror("write");
                exit(1);
            }
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
            FD_SET(client_fd, &all_fds);
            // set new player
            struct player *pplayer = malloc (sizeof (struct player));
            pplayer->fd = client_fd;
            memset(pplayer->name, 0, MAXNAME+1);
            pplayer->prev = NULL;
            pplayer->next = playerlist;
            // set pits & prev
            if (playerlist == NULL) {
                for (int i = 0; i < NPITS + 1; ++i){
                    if (i == NPITS) {
                        (pplayer->pits) [i] = 0;
                    } else {
                        (pplayer->pits) [i] = 4;
                    }
                }
            } else {
                pplayer->next->prev = pplayer;
                for (int i = 0; i < NPITS + 1; ++i){
                    if (i == NPITS) {
                        (pplayer->pits) [i] = 0;
                    } else {
                        (pplayer->pits) [i] = compute_average_pebbles();
                    }
                }
            }
            playerlist = pplayer;
        }

        // check the clients
        struct player *p = playerlist;
        while (p){
            if (FD_ISSET(p->fd, &listen_fds)) {
                // player entered the name
                // NOTE: new player entry point
                if (strlen(p->name)==0){
                    if (game_empty()) whose_turn = p;
                    struct player *temp = p->next;
                    int error = read_name(p);
                    if (error < 0) {
                        FD_CLR(p->fd, &all_fds);
                        remove_player(p);
                        p = temp;
                        continue;
                    }
                    char message[MAXMESSAGE];
                    snprintf(message, MAXMESSAGE, "%s joined the game.\r\n", p->name);
                    broadcast (message);
                    struct player *p_next = p->next;
                    move_to_front(p);
                    game_display(NULL);
                    if (p == whose_turn) {
                        write(p->fd, "Your move?\r\n", strlen("Your move?\r\n"));
                        p = p_next;
                        continue;
                    } else {
                        char buf[MAXMESSAGE];
                        snprintf(buf, MAXMESSAGE, "It is %s's move.\r\n", whose_turn->name);
                        broadcast(buf);
                        write(whose_turn->fd, "Your move?\r\n", strlen("Your move?\r\n"));
                        p = p_next;
                        continue;
                    }
                } else {
                    // player entered a digit
                    if (whose_turn == p){
                        // play here
                        struct player *p_next = p->next;
                        int index = read_digit(p->fd, p, &all_fds);
                        if (index < 0){
                            // remove the player from list
                            FD_CLR(p->fd, &all_fds);
                            remove_player(p);
                            p = p_next;
                            continue;
                        }
                        int change_turn = play(index);
                        if (change_turn) next_turn();
                        game_display(NULL);
                        char message[MAXMESSAGE];
                        snprintf(message, MAXMESSAGE, "%s chose pit#%d.\r\n", p->name, index);
                        broadcast(message);
                        snprintf(message, MAXMESSAGE, "It is %s's move.\r\n", whose_turn->name);
                        broadcast(message);
                        write(whose_turn->fd, "Your move?\r\n", strlen("Your move?\r\n"));
                        p = p->next;
                        continue;
                    } else {
                        struct player *p_next = p->next;
                        char misc [MAXMESSAGE];
                        int num_read;
                        if ((num_read = read(p->fd, &(misc[0]), MAXMESSAGE)) > 0){
                            write(p->fd, "It is not your move.\r\n", strlen("It is not your move.\r\n"));
                        }
                        if (num_read == 0){
                            char message[MAXMESSAGE];
                            snprintf(message, MAXMESSAGE, "%s left the game.\r\n", p->name);
                            broadcast(message);
                            game_display(p);
                            // remove the player from list
                            FD_CLR(p->fd, &all_fds);
                            remove_player(p);
                            snprintf(message, MAXMESSAGE, "It is %s's move.\r\n", whose_turn->name);
                            broadcast(message);
                            write(whose_turn->fd, "Your move?\r\n", strlen("Your move?\r\n"));
                        }
                        p = p_next;
                        continue;
                    }
                }
            }
            p = p->next;
            continue;
        }
    }

    broadcast("Game over!\r\n");
    printf("Game over!\n");
    for (struct player *p = playerlist; p; p = p->next) {
        int points = 0;
        for (int i = 0; i <= NPITS; i++) {
            points += p->pits[i];
        }
        printf("%s has %d points\r\n", p->name, points);
        snprintf(msg, MAXMESSAGE, "%s has %d points\r\n", p->name, points);
        broadcast(msg);
    }

    return 0;
}

// Actually plays the game, requires at least one player in the game
int play (int index){
    int stone = (whose_turn->pits)[index];
    int pit_num = index + 1;
    struct player *current = whose_turn;
    (whose_turn->pits)[index] = 0;
    while (stone > 0) {
        (current->pits)[pit_num] += 1;
        stone--;
        if (stone == 0 && pit_num == NPITS && current == whose_turn) {
            return 0;
        }
        pit_num++;
        if (pit_num >= NPITS) {
            if (current == whose_turn && pit_num == NPITS) continue;
            current = next_player(current);
            pit_num = 0;
        }
    }
    return 1;
}

// Reads the digit representing the index of the pit
int read_digit(int fd, struct player *p, fd_set *all_fds){
    int num_read;
    char digit[MAXNAME+1];
    long ret;
    while ((num_read = read(fd, &(digit[0]), MAXNAME)) > 0){
        ret = strtol(digit, NULL, 10);
        if (digit[0] == '\r' || digit[0] == '\n' || ret < 0 || ret > 5 || (p->pits)[ret]==0) {
            char *msg = "Invalid index, enter again.\r\n";
            // player disconnected
            if (write (fd, msg, strlen(msg)) != strlen (msg)){
                char message[MAXMESSAGE];
                snprintf(message, MAXMESSAGE, "%s left the game.\r\n", p->name);
                exclusive_broadcast(message, p);
                game_display(p);
                if (p == whose_turn) {
                    struct player *temp = whose_turn;
                    next_turn();
                    if (whose_turn == temp) {
                        whose_turn = NULL;
                        return -1;
                    }
                }
                snprintf(message, MAXMESSAGE, "It is %s's move.\r\n", whose_turn->name);
                broadcast(message);
                write(whose_turn->fd, "Your move?\r\n", strlen("Your move?\r\n"));
                return -1;
            }
            continue;
        }
        break;
    }
    if (num_read == -1){
        perror ("read:name");
        exit(1);
    }
    if (num_read  == 0) {
        char message[MAXMESSAGE];
        snprintf(message, MAXMESSAGE, "%s left the game.\r\n", p->name);
        exclusive_broadcast(message, p);
        game_display(p);
        if (p == whose_turn) {
            struct player *temp = whose_turn;
            next_turn();
            if (whose_turn == temp) {
                whose_turn = NULL;
                return -1;
            }
        }
        snprintf(message, MAXMESSAGE, "It is %s's move.\r\n", whose_turn->name);
        exclusive_broadcast(message, p);
        write(whose_turn->fd, "Your move?\r\n", strlen("Your move?\r\n"));
        return -1;
    }
    return ret;
}

// Reads the name of the player from fd and return it
// otherwise return NULL
int read_name (struct player *p){
    int fd = p->fd;
    int index = 0;
    int num_read;
    char *name = malloc (sizeof(char) * (MAXNAME+1));
    int eof = 0;
    while (index < MAXNAME &&
        (num_read = read(fd, &(name[index]), MAXNAME)) > 0) {
            for(int i = 0; i < num_read; ++i){
                // check if name too long
                if (index + i >= MAXNAME && ((name[index + i] != '\n') ||
                                            (name[index + i] != '\r'))) {
                        close (fd);
                        return -1;
                }
                // check network newline
                if (name[index + i] == '\n' || name[index + i] == '\r'){
                    name[index + i] = '\0';
                    // check empty string
                    if (index + i == 0) {
                        close (fd);
                        return -1;
                    }
                    eof = 1;
                    break;
                }
            }
            index += num_read;
            if (eof) break;
    }
    if (num_read == -1){
        perror ("read:name");
        exit(1);
    }

    if (num_read  == 0) return -1;

    // checks if the name already exists
    for (struct player *p = playerlist; p; p = p->next) {
        if (strcmp(p->name, name) == 0){
            close (fd);
            return -1;
        }
    }
    strcpy(p->name, name);
    free (name);
    return 0;
}

// Moves the node to the front of the list
void move_to_front (struct player *p){
    if (playerlist == p) return;
    if (p->prev != NULL){
        p->prev->next = p->next;
        if (p->next != NULL) {
            p->next->prev = p->prev;
        }
        p->prev = NULL;
        p->next = playerlist;
    }
    playerlist->prev = p;
    playerlist = p;
}

// Mutates whose_turn to the next player
void next_turn (){
    struct player *current = whose_turn->prev;
    while (1){
        if (game_empty()) break;
        if (current == NULL){
            current = last_player();
        } else if (strlen(current->name)!=0){
            whose_turn = current;
            break;
        } else {
            current = current->prev;
        }
    }
}

// Returns the next player in game
struct player *next_player (struct player *p){
    struct player *current = p->prev;
    while (1){
        if (current == NULL){
            current = last_player();
        } else if (strlen(current->name)!=0){
            break;
        } else {
            current = current->prev;
        }
    }
    return current;
}

// Returns the pointer to last player in the list
struct player *last_player () {
    struct player *ret = playerlist;
    for (struct player *p = playerlist; p; p = p->next){
        if (p->next == NULL) ret = p;
    }
    return ret;
}

// Removes the player p from list and free memory
void remove_player (struct player *p){
    if (whose_turn == p){
        next_turn();
    }
    if (playerlist == p) {
        playerlist = p->next;
    }
    if (p->prev == NULL) {
        if (p->next == NULL) {
            free(p);
        } else {
            p->next->prev = NULL;
            free(p);
        }
    } else {
        // prev is not NULL
        if (p->next == NULL) {
            p->prev->next = NULL;
            free(p);
        } else {
            p->prev->next = p->next;
            p->next->prev = p->prev;
            free(p);
        }
    }
}

// Checks if no player has entered or every player has left
int game_empty (){
    for (struct player *p = playerlist; p; p = p->next){
        if (strlen(p->name)!=0) return 0;
    }
    return 1;
}

// Displays the current game, exclude exclude
void game_display (struct player *exclude){
    struct player *p = playerlist;
    while (p){
        if (strlen(p->name)==0 || p == exclude) {
            p = p->next;
            continue;
        }
        char msg [MAXMESSAGE];
        snprintf(msg, MAXMESSAGE, "%s:  [0]%d [1]%d [2]%d [3]%d [4]%d [5]%d  [end pit]%d\r\n",
                p->name, (p->pits)[0], (p->pits)[1], (p->pits)[2], (p->pits)[3], (p->pits)[4],
                (p->pits)[5], (p->pits)[6]);
        if (exclude == NULL) {
            broadcast(msg);
        } else {
            exclusive_broadcast(msg, exclude);
        }
        p = p->next;
    }
    return;
}

// Broadcasts s to all players
void broadcast(char *s) {
    for (struct player *p = playerlist; p; p = p->next) {
        if (write(p->fd, s, strlen(s)) < 0){
            perror("write");
            exit(1);
        }
    }
}

// Broadcasts s to all players except 'exclude'
void exclusive_broadcast(char *s, struct player *exclude) {
    for (struct player *p = playerlist; p; p = p->next) {
        if (p == exclude) continue;
        if (write(p->fd, s, strlen(s)) < 0){
            perror("write");
            exit(1);
        }
    }
}

void parseargs(int argc, char **argv) {
    int c, status = 0;
    while ((c = getopt(argc, argv, "p:")) != EOF) {
        switch (c) {
        case 'p':
            port = strtol(optarg, NULL, 0);
            break;
        default:
            status++;
        }
    }
    if (status || optind != argc) {
        fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
        exit(1);
    }
}


void makelistener() {
    struct sockaddr_in r;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    int on = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
               (const char *) &on, sizeof(on)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    memset(&r, '\0', sizeof(r));
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *)&r, sizeof(r))) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
}



/* call this BEFORE linking the new player in to the list */
int compute_average_pebbles() {
    struct player *p;
    int i;

    if (playerlist == NULL) {
        return NPEBBLES;
    }

    int nplayers = 0, npebbles = 0;
    for (p = playerlist; p; p = p->next) {
        nplayers++;
        for (i = 0; i < NPITS; i++) {
            npebbles += p->pits[i];
        }
    }
    return ((npebbles - 1) / nplayers / NPITS + 1);  /* round up */
}


int game_is_over() { /* boolean */
    int i;

    if (!playerlist) {
       return 0;  /* we haven't even started yet! */
    }

    for (struct player *p = playerlist; p; p = p->next) {
        int is_all_empty = 1;
        for (i = 0; i < NPITS; i++) {
            if (p->pits[i]) {
                is_all_empty = 0;
            }
        }
        if (is_all_empty) {
            return 1;
        }
    }
    return 0;
}
