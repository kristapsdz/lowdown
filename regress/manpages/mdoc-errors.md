section: 2

# ERRORS

*write()*, *pwrite()*, *writev()*, and *pwritev()* will fail and the file pointer will remain unchanged
if:

*[EBADF]*
: *d* is not a valid descriptor open for writing.

*[EFBIG]*
: An attempt was made to write a file that exceeds the process's file size
limit or the maximum file size.

*[ENOSPC]*
: There is no free space remaining on the file system containing the file.

*[EDQUOT]*
: The user's quota of disk blocks on the file system containing the file has
been exhausted.

*[EINTR]*
: A write to a slow device (i.e. one that might block for an arbitrary amount
of time) was interrupted by the delivery of a signal before any data could
be written.

*[EIO]*
: An I/O error occurred while reading from or writing to the file system.

*[EFAULT]*
: Part of buf points outside the process's allocated address space.

In addition, *write()* and *writev()* may return the following errors:

*[EPIPE]*
: An attempt is made to write to a pipe that is not open for reading by any
process.

*[EPIPE]*
: An attempt is made to write to a socket of type *SOCK\_STREAM* that is not
connected to a peer socket.

*[EAGAIN]*
: The file was marked for non-blocking I/O, and no data could be written
immediately.

*[ENETDOWN]*
: The destination address specified a network that is down.

*[EDESTADDRREQ]*
: The destination is no longer available when writing to a Unix-domain
datagram socket on which *connect(2)* had been used to set a destination
address.

*[EIO]*
: The process is a member of a background process attempting to write to its
controlling terminal, *TOSTOP* is set on the terminal, the process isn't
ignoring the *SIGTTOUT* signal and the thread isn't blocking the *SIGTTOUT*
signal, and either the process was created with *vfork(2)* and hasn't
successfully executed one of the exec functions or the process group is
orphaned.

*write()* and *pwrite()* may return the following error:

*[EINVAL]*
: nbytes was larger than *SSIZE\_MAX*.

*pwrite()* and *pwritev()* may return the following error:

*[EINVAL]*
: offset was negative.

*[ESPIPE]*
: *d* is associated with a pipe, socket, FIFO, or tty.

*writev()* and *pwritev()* may return one of the following errors:

*[EINVAL]*
: iovcnt was less than or equal to 0, or greater than *IOV\_MAX*.

*[EINVAL]*
: The sum of the *iov\_len* values in the iov array overflowed an *ssize\_t*.

*[EFAULT]*
: Part of iov points outside the process's allocated address space.

*[ENOBUFS]*
: The system lacked sufficient buffer space or a queue was full.

