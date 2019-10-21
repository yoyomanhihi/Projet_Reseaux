#ifndef __READ_WRITE_LOOP_H_
#define __READ_WRITE_LOOP_H_

/* Loop reading a socket and printing to stdout,
 * while reading stdin and writing to the socket
 * @sfd: The socket file descriptor. It is both bound and connected.
 * @return: as soon as stdin signals EOF
 */

//Ce code a été repris du code du projet de l'année précédente. Les membres du groupe à l'époque étaient Matteo Snellings et moi-même, Simon Kellen.

int read_write_loop(const int sfd, char *filename);

#endif

