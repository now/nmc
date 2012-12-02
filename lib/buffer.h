struct buffer;

struct buffer *buffer_new(const char *string, size_t length);
struct buffer *buffer_new_empty(void);
void buffer_free(struct buffer *buffer);
struct buffer *buffer_append(struct buffer *buffer, const char *string, size_t length);
char *buffer_str(struct buffer *buffer);
char *buffer_str_free(struct buffer *buffer);
