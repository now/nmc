typedef int32_t uchar;

bool uc_issolid(uchar c);

#define U_BAD_INPUT_CHAR ((uchar)-1)

extern const char * const u_skip_lengths;

#define u_next(p) ((p) + u_skip_lengths[*(const unsigned char *)(p)])

uchar u_dref(const char *u) __attribute__((pure));

static inline uchar
u_lref(const char *u, size_t *length)
{
        uchar c = u_dref(u);
        *length = u_next(u) - u;
        return c;
}

static inline uchar
u_iref(const char *u, size_t *length)
{
        if (*(const unsigned char *)u < 0x80) {
                *length = 1;
                return *u;
        } else {
                return u_lref(u, length);
        }
}
