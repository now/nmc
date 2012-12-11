struct buffer {
        size_t allocated;
        size_t length;
        char *content;
};

#define BUFFER_INIT { 0, 0, NULL }

struct buffer *buffer_new(const char *string, size_t length);
void buffer_free(struct buffer *buffer);
struct buffer *buffer_append(struct buffer *buffer, const char *string, size_t length);
char *buffer_str(struct buffer *buffer);
