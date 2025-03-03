/*
 * wiggle - apply rejected patches
 *
 * Copyright (C) 2003 Neil Brown <neilb@cse.unsw.edu.au>
 * Copyright (C) 2010-2013 Neil Brown <neilb@suse.de>
 *
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.
 *
 *    Author: Neil Brown
 *    Email: <neilb@suse.de>
 */

/*
 * split patch or merge files.
 */

#include	"wiggle.h"
#include	<stdlib.h>

/* skip 'cp' past the new '\n', or all the way to 'end' */
static void skip_eol(char **cp, char *end)
{
	char *c = *cp;
	while (c < end && *c != '\n')
		c++;
	if (c < end)
		c++;
	*cp = c;
}

/* copy one line, or to end, from 'cp' into the stream, extending
 * the stream.
 */
static void copyline(struct stream *s, char **cp, char *end)
{
	char *from = *cp;
	char *to = s->body+s->len;

	while (from < end && *from != '\n')
		*to++ = *from++;
	if (from < end)
		*to++ = *from++;
	s->len = to-s->body;
	*cp = from;
}

int split_patch(struct stream f, struct stream *f1, struct stream *f2)
{
	struct stream r1, r2;
	int chunks = 0;
	char *cp, *end;
	int state = 0;
	int acnt = 0, bcnt = 0;
	int a, b, c, d;
	int lineno = 0;
	char before[100], after[100];
	char func[100];

	f1->body = f2->body = NULL;

	r1.body = xmalloc(f.len);
	r2.body = xmalloc(f.len);
	r1.len = r2.len = 0;
	func[0] = 0;

	cp = f.body;
	end = f.body+f.len;
	while (cp < end) {
		/* state:
		 *   0   not in a patch
		 *   1   first half of context
		 *   2   second half of context
		 *   3   unified
		 */
		lineno++;
		switch (state) {
		case 0:
			if (sscanf(cp, "@@ -%99s +%99s @@%99[^\n]", before, after, func) >= 2) {
				int ok = 1;
				if (sscanf(before, "%d,%d", &a, &b) == 2)
					acnt = b;
				else if (sscanf(before, "%d", &a) == 1)
					acnt = 1;
				else
					ok = 0;

				if (sscanf(after, "%d,%d", &c, &d) == 2)
					bcnt = d;
				else if (sscanf(after, "%d", &c) == 1)
					bcnt = 1;
				else
					ok = 0;
				if (ok)
					state = 3;
				else
					state = 0;
			} else if (sscanf(cp, "*** %d,%d ****", &a, &b) == 2) {
				acnt = b-a+1;
				state = 1;
			} else if (sscanf(cp, "--- %d,%d ----", &c, &d) == 2) {
				bcnt = d-c+1;
				state = 2;
			} else
				sscanf(cp, "***************%99[^\n]", func);

			skip_eol(&cp, end);
			if (state == 1 || state == 3) {
				char *f;
				int slen;
				/* Reserve enough space for 3 integers separated
				 * by a single space, and prefixed and terminated
				 * with a null character.
				 */
				char buf[(3*12)+1];
				buf[0] = 0;
				chunks++;
				slen = sprintf(buf+1, "%5d %5d %5d", chunks, a, acnt)+1;
				memcpy(r1.body+r1.len, buf, slen);
				r1.len += slen;
				f = func;
				while (*f == ' ')
					f++;
				if (*f) {
					r1.body[r1.len++] = ' ';
					strcpy(r1.body + r1.len, f);
					r1.len += strlen(f);
				}
				r1.body[r1.len++] = '\n';
				r1.body[r1.len++] = '\0';
			}
			if (state == 2 || state == 3) {
				int slen;
				/* Reserve enough space for 3 integers separated
				 * by a single space, prefixed with a null character
				 * and terminated with a new line and null character.
				 */
				char buf[(3*12)+2];
				buf[0] = 0;
				slen = sprintf(buf+1, "%5d %5d %5d\n", chunks, c, bcnt)+2;
				memcpy(r2.body+r2.len, buf, slen);
				r2.len += slen;
			}
			if (state)
				func[0] = 0;
			break;
		case 1:
			if ((*cp == ' ' || *cp == '!' || *cp == '-' || *cp == '+')
			    && cp[1] == ' ') {
				cp += 2;
				copyline(&r1, &cp, end);
				acnt--;
				if (acnt == 0)
					state = 0;
			} else {
				fprintf(stderr, "%s: bad context patch at line %d\n",
					Cmd, lineno);
				return 0;
			}
			break;
		case 2:
			if ((*cp == ' ' || *cp == '!' || *cp == '-' || *cp == '+')
			    && cp[1] == ' ') {
				cp += 2;
				copyline(&r2, &cp, end);
				bcnt--;
				if (bcnt == 0)
					state = 0;
			} else {
				fprintf(stderr, "%s: bad context patch/2 at line %d\n",
					Cmd, lineno);
				return 0;
			}
			break;
		case 3:
			if (*cp == ' ') {
				char *cp2;
				cp++;
				cp2 = cp;
				copyline(&r1, &cp, end);
				copyline(&r2, &cp2, end);
				acnt--; bcnt--;
			} else if (*cp == '-') {
				cp++;
				copyline(&r1, &cp, end);
				acnt--;
			} else if (*cp == '+') {
				cp++;
				copyline(&r2, &cp, end);
				bcnt--;
			} else if (*cp == '\n') {
				/* Empty line - treat like " \n" - a blank line in both */
				char *cp2 = cp;
				copyline(&r1, &cp, end);
				copyline(&r2, &cp2, end);
				acnt --; bcnt--;
			} else {
				fprintf(stderr, "%s: bad unified patch at line %d\n",
					Cmd, lineno);
				return 0;
			}
			if (acnt <= 0 && bcnt <= 0)
				state = 0;
			break;
		}
	}
	if (r1.len > f.len || r2.len > f.len)
		abort();
	*f1 = r1;
	*f2 = r2;
	return chunks;
}

/*
 * extract parts of a "diff3 -m" or "wiggle -m" output
 */
int split_merge(struct stream f, struct stream *f1, struct stream *f2, struct stream *f3)
{
	int state = 0;
	char *cp, *end;
	struct stream r1, r2, r3;
	f1->body = NULL;
	f2->body = NULL;

	r1.body = xmalloc(f.len);
	r2.body = xmalloc(f.len);
	r3.body = xmalloc(f.len);
	r1.len = r2.len = r3.len = 0;

	cp = f.body;
	end = f.body+f.len;
	while (cp < end) {
		/* state:
		 *  0 not in conflict
		 *  1 in file 1 of conflict
		 *  2 in file 2 of conflict
		 *  3 in file 3 of conflict
		 *  4 in file 2 but expecting 1/3 next
		 *  5 in file 1/3
		 */
		int len = end-cp;
		switch (state) {
		case 0:
			if (len >= 8 &&
			    strncmp(cp, "<<<<<<<", 7) == 0 &&
			    (cp[7] == ' ' || cp[7] == '\n')
				) {
				char *peek;
				state = 1;
				skip_eol(&cp, end);
				/* diff3 will do something a bit strange in
				 * the 1st and 3rd sections are the same.
				 * it reports
				 * <<<<<<<
				 * 2nd
				 * =======
				 * 1st and 3rd
				 * >>>>>>>
				 * Without a ||||||| at all.
				 * so to know if we are in '1' or '2', skip forward
				 * having a peek.
				 */
				peek = cp;
				while (peek < end) {
					if (end-peek >= 8 &&
					    (peek[7] == ' ' || peek[7] == '\n')) {
						if (strncmp(peek, "|||||||", 7) == 0 ||
						    strncmp(peek, ">>>>>>>", 7) == 0)
							break;
						else if (strncmp(peek, "=======", 7) == 0) {
							state = 4;
							break;
						}
					}
					skip_eol(&peek, end);
				}
			} else {
				char *cp2 = cp;
				copyline(&r1, &cp2, end);
				cp2 = cp;
				copyline(&r2, &cp2, end);
				copyline(&r3, &cp, end);
			}
			break;
		case 1:
			if (len >= 8 &&
			    strncmp(cp, "|||||||", 7) == 0 &&
			    (cp[7] == ' ' || cp[7] == '\n')
				) {
				state = 2;
				skip_eol(&cp, end);
			} else
				copyline(&r1, &cp, end);
			break;
		case 2:
			if (len >= 8 &&
			    strncmp(cp, "=======", 7) == 0 &&
			    (cp[7] == ' ' || cp[7] == '\n')
				) {
				state = 3;
				skip_eol(&cp, end);
			} else
				copyline(&r2, &cp, end);
			break;
		case 3:
			if (len >= 8 &&
			    strncmp(cp, ">>>>>>>", 7) == 0 &&
			    (cp[7] == ' ' || cp[7] == '\n')
				) {
				state = 0;
				skip_eol(&cp, end);
			} else
				copyline(&r3, &cp, end);
			break;
		case 4:
			if (len >= 8 &&
			    strncmp(cp, "=======", 7) == 0 &&
			    (cp[7] == ' ' || cp[7] == '\n')
				) {
				state = 5;
				skip_eol(&cp, end);
			} else
				copyline(&r2, &cp, end);
			break;
		case 5:
			if (len >= 8 &&
			    strncmp(cp, ">>>>>>>", 7) == 0 &&
			    (cp[7] == ' ' || cp[7] == '\n')
				) {
				state = 0;
				skip_eol(&cp, end);
			} else {
				char *t = cp;
				copyline(&r1, &t, end);
				copyline(&r3, &cp, end);
			}
			break;
		}
	}
	if (cp > end)
		abort();
	*f1 = r1;
	*f2 = r2;
	*f3 = r3;
	return state == 0;
}
