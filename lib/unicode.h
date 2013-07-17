typedef int32_t uchar;

PURE bool uc_issolid(uchar c);
PURE bool uc_isaletterornumeric(uchar c);
PURE bool uc_isformatorextend(uchar c);

PURE bool u_isafteraletterornumeric(const char *string, const char *p);
void u_word_breaks(const char *string, size_t n, bool *breaks);

size_t u_width(const char *string, size_t length);

#define U_BAD_INPUT_CHAR ((uchar)-1)

extern const char * const u_skip_lengths;

#define u_next(p) ((p) + u_skip_lengths[*(const unsigned char *)(p)])
PURE char *u_prev_s(const char *string, const char *p);

PURE uchar u_dref(const char *u);

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
