struct buffer {
        size_t allocated;
        size_t length;
        char *content;
};

#define BUFFER_INIT { 0, 0, NULL }

struct buffer *buffer_new(const char *string, size_t length);
void buffer_free(struct buffer *buffer);
bool buffer_append(struct buffer *buffer, const char *string, size_t length);
bool buffer_append_c(struct buffer *buffer, char c, size_t n);
bool buffer_read(struct buffer *buffer, int fd, size_t n);
char *buffer_str(struct buffer *buffer);
