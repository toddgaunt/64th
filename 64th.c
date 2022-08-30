#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>

#include "arg.h"

#define NAME "64th"

#define DEFAULT_DATA_SIZE 4096

/* Maximum length of a word's symbol */
#define WORD_SIZE 255

/* Memory layout */
#define STACK_ADDR 0
#define STACK_SIZE 512
#define RSTACK_ADDR (STACK_ADDR + STACK_SIZE)
#define RSTACK_SIZE 512
#define DATA_ADDR (RSTACK_ADDR + RSTACK_SIZE)

/* Error codes. */
#define ERR_OK 0
#define ERR_UNDERFLOW -1
#define ERR_OVERFLOW -2
#define ERR_FAULT -3

typedef unsigned char byte;
typedef unsigned long long cell;
typedef long long scell;

/* Data space variable addresses */
enum {
	STATE = DATA_ADDR + 0,
	HERE = DATA_ADDR + 1,
	LATEST = DATA_ADDR + 2,
	// The start of the unused data segment
	DATA = DATA_ADDR + 3,
};

/* Interpreter states. */
enum {
	INTERACTIVE = 0,
	COLON = 1,
	COMPILE = 2,
};

/* VM opcodes. */
enum {
	NEXT,
	DOCOL,
	DOSEM,
	DOLIT,
	PRINT,
	LOAD,
	STORE,
	DROP,
	SWAP,
	DUP,
	OVER,
	ROT,
	PUSH,
	PULL,
	NOT,
	AND,
	OR,
	XOR,
	ADD,
	SUB,
	MUL,
	DIV,
	LSH,
	RSH,
	EQ,
	LT,
};

struct word {
	byte flags;
	char *symbol;
	cell addr;
	int inputs;
	int outputs;
	struct word *next;
};

struct v64th {
	cell sp;
	cell rsp;

	cell size;
	union {
		cell *memory;
		byte *bytes;
	};
};

struct word *dictionary = NULL;

bool
is_space(char ch)
{
	return ch == ' '
	    || ch == '\r'
	    || ch == '\t'
	    || ch == '\n'
	    || ch == '\v';
}

char const *
sigilfrom(int state)
{
	switch (state) {
	case COLON:       return "..";
	case COMPILE:     return "..";
	case INTERACTIVE: return "ok";
	}
	return "??";
}

void
prompt(int state)
{
	fprintf(stderr, "%s> ", sigilfrom(state));
}

int
read_word(char *buf, size_t buf_size, bool *newline)
{
	size_t i;
	int ch;
	for (i = 0; i < buf_size - 1; ++i) {
		ch = getc(stdin);
		if (is_space(ch) || ch == EOF) {
			break;
		}
		buf[i] = ch;
	}
	buf[i] = '\0';
	if (newline)
		*newline = ch == '\n';
	if (ch == EOF)
		return -1;
	return i;
}

struct word *
lookup_word(char const *symbol)
{
	struct word *entry;
	for (entry = dictionary; entry != NULL; entry = entry->next) {
		if (0 == strcmp(entry->symbol, symbol)) {
			return entry;
		}
	}
	return NULL;
}

struct word *
create_word(char const *symbol, cell addr, int inputs, int outputs)
{
	struct word *entry = calloc(1, sizeof(*entry));
	entry->symbol = calloc(1, strlen(symbol) + 1);
	strcpy(entry->symbol, symbol);
	entry->next = dictionary;
	entry->addr = addr;
	entry->inputs = inputs;
	entry->outputs = outputs;

	dictionary = entry;
	return entry;
}

/* Stack functions */

#if 0
static cell
pop(struct v64th *v)
{
	/* This check should only trigger if there is a bug. */
	if (v->sp == STACK_ADDR + STACK_SIZE) {
		fprintf(stderr, "fatal stack underflow\n");
		exit(EXIT_FAILURE);
	}
	return v->memory[v->sp++];
}
#endif

static void
push(struct v64th *v, cell n)
{
	/* This check should only trigger if there is a bug. */
	if (v->sp == STACK_ADDR) {
		fprintf(stderr, "fatal stack overflow\n");
		exit(EXIT_FAILURE);
	}
	v->memory[--v->sp] = n;
}

bool
fault(struct v64th *v, cell addr)
{
	return addr < DATA_ADDR || addr >= v->size;
}

bool
overflow(struct v64th *v, unsigned int n)
{
	return v->sp <= STACK_ADDR + n;
}

bool
underflow(struct v64th *v, unsigned int n)
{
	return (v->sp) > (STACK_ADDR + STACK_SIZE - n);
}

/* Return stack functions. */

static cell
rpop(struct v64th *v)
{
	/* This check should only trigger if there is a bug. */
	if (v->rsp == RSTACK_ADDR + RSTACK_SIZE) {
		fprintf(stderr, "fatal rstack underflow\n");
		exit(EXIT_FAILURE);
	}
	return v->memory[v->rsp++];
}

static void
rpush(struct v64th *v, cell addr)
{
	/* This check should only trigger if there is a bug. */
	if (v->rsp == RSTACK_ADDR) {
		fprintf(stderr, "fatal rstack overflow\n");
		exit(EXIT_FAILURE);
	}
	v->memory[--v->rsp] = addr;
}

/* Pcode functions. */

void
compile(struct v64th *v, cell addr)
{
	if (v->memory[HERE] >= v->size) {
		fprintf(stderr, "memory overflow\n");
		exit(EXIT_FAILURE);
	}
	v->memory[v->memory[HERE]++] = addr;
}

int
exec(struct v64th *v, cell addr)
{
	cell i, w, p;

	i = w = p = addr;
	for (;;) {
		//printf("%u, %u, %u\n", i, w, p);
		// Addressing memory outside of the data segment is dangerous
		if (fault(v, i) || fault(v, w) || fault(v, p))
			return ERR_FAULT;

		switch (v->memory[p]) {
		case NEXT:
			w = v->memory[i];
			i++;
			p = w;
			break;
		case DOCOL:
			rpush(v, i);
			i = w + 1;
			/* NEXT */
			w = v->memory[i];
			i++;
			p = w;
			break;
		case DOSEM:
			i = rpop(v);
			if (v->rsp == RSTACK_ADDR + RSTACK_SIZE)
				goto end;
			/* NEXT */
			w = v->memory[i];
			i++;
			p = w;
			break;
		case DOLIT:
			push(v, v->memory[i]);
			i++;
			p++;
			break;
		case PRINT: // ( n -- )
			if (underflow(v, 1))
				return ERR_UNDERFLOW;
			printf("%lld\n", v->memory[v->sp]);
			v->sp++;
			p++;
			break;
		case LOAD:
			if (underflow(v, 1))
				return ERR_UNDERFLOW;
			if (fault(v, v->memory[v->sp]))
				return ERR_FAULT;
			v->memory[v->sp] = v->memory[v->memory[v->sp]];
			p++;
			break;
		case STORE:
			if (overflow(v, 2))
				return ERR_UNDERFLOW;
			if (fault(v, v->memory[v->sp]))
				return ERR_FAULT;
			v->memory[v->memory[v->sp]] = v->memory[v->sp+1];
			v->sp += 2;
			p++;
			break;
		case DROP: // ( x -- )
			if (underflow(v, 1))
				return ERR_UNDERFLOW;
			v->sp++;
			p++;
			break;
		case SWAP: // ( x y -- y x )
			if (underflow(v, 2))
				return ERR_UNDERFLOW;
			/* XOR swap. */
			v->memory[v->sp+0] ^= v->memory[v->sp+1];
			v->memory[v->sp+1] ^= v->memory[v->sp+0];
			v->memory[v->sp+0] ^= v->memory[v->sp+1];
			p++;
			break;
		case DUP: // ( x -- x x )
			if (underflow(v, 1))
				return ERR_UNDERFLOW;
			if (overflow(v, 1))
				return ERR_OVERFLOW;
			v->memory[v->sp - 1] = v->memory[v->sp];
			v->sp--;
			p++;
			break;
		case OVER: // ( y x -- y x y )
			if (overflow(v, 1))
				return ERR_OVERFLOW;
			if (underflow(v, 1))
				return ERR_UNDERFLOW;
			v->memory[v->sp-1] = v->memory[v->sp+1];
			v->sp--;
			p++;
			break;
		case ROT: // ( x y z -- y z x )
			if (underflow(v, 3))
				return ERR_UNDERFLOW;
			cell x = v->memory[v->sp+2];
			cell y = v->memory[v->sp+1];
			cell z = v->memory[v->sp+0];
			v->memory[v->sp+2] = y;
			v->memory[v->sp+1] = z;
			v->memory[v->sp+0] = x;
			p++;
			break;
		case PUSH: // ( x -- ) ( R: -- x )
			//TODO
			break;
		case PULL: // ( -- x ) ( R: x -- )
			//TODO
			break;
		case NOT: // ( x y -- z )
			if (underflow(v, 1))
				return ERR_UNDERFLOW;
			v->memory[v->sp] = ~v->memory[v->sp];
			p++;
			break;
		case AND: // ( x y -- z )
			if (underflow(v, 2))
				return ERR_UNDERFLOW;
			v->memory[v->sp+1] = v->memory[v->sp+1] & v->memory[v->sp];
			v->sp++;
			p++;
			break;
		case OR: // ( x y -- z )
			if (underflow(v, 2))
				return ERR_UNDERFLOW;
			v->memory[v->sp+1] = v->memory[v->sp+1] | v->memory[v->sp];
			v->sp++;
			p++;
			break;
		case XOR: // ( x y -- z )
			if (underflow(v, 2))
				return ERR_UNDERFLOW;
			v->memory[v->sp+1] = v->memory[v->sp+1] ^ v->memory[v->sp];
			v->sp++;
			p++;
			break;
		case ADD: // ( x y -- z )
			if (underflow(v, 2))
				return ERR_UNDERFLOW;
			v->memory[v->sp+1] = v->memory[v->sp+1]+v->memory[v->sp];
			v->sp++;
			p++;
			break;
		case SUB: // ( x y -- z )
			if (underflow(v, 2))
				return ERR_UNDERFLOW;
			v->memory[v->sp+1] = v->memory[v->sp+1] - v->memory[v->sp];
			v->sp++;
			p++;
			break;
		case MUL: // ( x y -- z )
			if (underflow(v, 2))
				return ERR_UNDERFLOW;
			v->memory[v->sp+1] = v->memory[v->sp+1] * v->memory[v->sp];
			v->sp++;
			p++;
			break;
		case DIV: // ( x y -- z )
			if (underflow(v, 2))
				return ERR_UNDERFLOW;
			v->memory[v->sp+1] = v->memory[v->sp+1] / v->memory[v->sp];
			v->sp++;
			p++;
			break;
		case LSH: // ( x y -- z )
			if (underflow(v, 2))
				return ERR_UNDERFLOW;
			v->memory[v->sp+1] = v->memory[v->sp+1] << v->memory[v->sp];
			v->sp++;
			p++;
			break;
		case RSH: // ( x y -- z )
			if (underflow(v, 2))
				return ERR_UNDERFLOW;
			v->memory[v->sp+1] = v->memory[v->sp+1] >> v->memory[v->sp];
			v->sp++;
			p++;
			break;
		case EQ: // ( x y -- t )
			if (underflow(v, 2))
				return ERR_UNDERFLOW;
			v->memory[v->sp+1] = -(v->memory[v->sp+1] == v->memory[v->sp]);
			v->sp++;
			p++;
			break;
		case LT: // ( x y -- t )
			if (underflow(v, 2))
				return ERR_UNDERFLOW;
			v->memory[v->sp+1] = -(v->memory[v->sp+1] < v->memory[v->sp]);
			v->sp++;
			p++;
			break;
		}
	}
end:
	return 0;
}

int
run(struct v64th *v)
{
	/* Primitives. */
	cell dosem_addr = v->memory[HERE];
	compile(v, DOSEM);

	cell dolit_addr = v->memory[HERE];
	compile(v, DOLIT);
	compile(v, NEXT);

	cell print_addr = v->memory[HERE];
	compile(v, PRINT);
	compile(v, NEXT);

	cell load_addr = v->memory[HERE];
	compile(v, LOAD);
	compile(v, NEXT);

	cell store_addr = v->memory[HERE];
	compile(v, STORE);
	compile(v, NEXT);

	//cell drop_addr = v->memory[HERE];
	compile(v, DROP);
	compile(v, NEXT);

	cell swap_addr = v->memory[HERE];
	compile(v, SWAP);
	compile(v, NEXT);

	cell dup_addr = v->memory[HERE];
	compile(v, DUP);
	compile(v, NEXT);

	cell over_addr = v->memory[HERE];
	compile(v, OVER);
	compile(v, NEXT);

	cell rot_addr = v->memory[HERE];
	compile(v, ROT);
	compile(v, NEXT);

	cell not_addr = v->memory[HERE];
	compile(v, NOT);
	compile(v, NEXT);

	cell and_addr = v->memory[HERE];
	compile(v, AND);
	compile(v, NEXT);

	cell or_addr = v->memory[HERE];
	compile(v, OR);
	compile(v, NEXT);

	cell xor_addr = v->memory[HERE];
	compile(v, XOR);
	compile(v, NEXT);

	cell add_addr = v->memory[HERE];
	compile(v, ADD);
	compile(v, NEXT);

	cell sub_addr = v->memory[HERE];
	compile(v, SUB);
	compile(v, NEXT);

	cell mul_addr = v->memory[HERE];
	compile(v, MUL);
	compile(v, NEXT);

	cell div_addr = v->memory[HERE];
	compile(v, DIV);
	compile(v, NEXT);

	cell lsh_addr = v->memory[HERE];
	compile(v, LSH);
	compile(v, NEXT);

	cell rsh_addr = v->memory[HERE];
	compile(v, RSH);
	compile(v, NEXT);

	cell eq_addr = v->memory[HERE];
	compile(v, EQ);
	compile(v, NEXT);

	cell lt_addr = v->memory[HERE];
	compile(v, LT);
	compile(v, NEXT);

	/* Words implementing the primitives. */
	create_word(".", v->memory[HERE], 1, 0);
	compile(v, DOCOL);
	compile(v, print_addr);
	compile(v, dosem_addr);

	create_word("@", v->memory[HERE], 1, 1);
	compile(v, DOCOL);
	compile(v, load_addr);
	compile(v, dosem_addr);

	create_word("!", v->memory[HERE], 2, 1);
	compile(v, DOCOL);
	compile(v, store_addr);
	compile(v, dosem_addr);

	create_word("drop", v->memory[HERE], 1, 0);
	compile(v, DOCOL);
	compile(v, swap_addr);
	compile(v, dosem_addr);

	create_word("swap", v->memory[HERE], 2, 2);
	compile(v, DOCOL);
	compile(v, swap_addr);
	compile(v, dosem_addr);

	create_word("dup", v->memory[HERE], 1, 2);
	compile(v, DOCOL);
	compile(v, dup_addr);
	compile(v, dosem_addr);

	create_word("over", v->memory[HERE], 2, 3);
	compile(v, DOCOL);
	compile(v, over_addr);
	compile(v, dosem_addr);

	create_word("rot", v->memory[HERE], 3, 3);
	compile(v, DOCOL);
	compile(v, rot_addr);
	compile(v, dosem_addr);

	create_word("not", v->memory[HERE], 1, 1);
	compile(v, DOCOL);
	compile(v, not_addr);
	compile(v, dosem_addr);

	create_word("and", v->memory[HERE], 2, 1);
	compile(v, DOCOL);
	compile(v, and_addr);
	compile(v, dosem_addr);

	create_word("or", v->memory[HERE], 2, 1);
	compile(v, DOCOL);
	compile(v, or_addr);
	compile(v, dosem_addr);

	create_word("xor", v->memory[HERE], 2, 1);
	compile(v, DOCOL);
	compile(v, xor_addr);
	compile(v, dosem_addr);

	create_word("+", v->memory[HERE], 2, 1);
	compile(v, DOCOL);
	compile(v, add_addr);
	compile(v, dosem_addr);

	create_word("-", v->memory[HERE], 2, 1);
	compile(v, DOCOL);
	compile(v, sub_addr);
	compile(v, dosem_addr);

	create_word("*", v->memory[HERE], 2, 1);
	compile(v, DOCOL);
	compile(v, mul_addr);
	compile(v, dosem_addr);

	create_word("/", v->memory[HERE], 2, 1);
	compile(v, DOCOL);
	compile(v, div_addr);
	compile(v, dosem_addr);

	create_word("<<", v->memory[HERE], 2, 1);
	compile(v, DOCOL);
	compile(v, lsh_addr);
	compile(v, dosem_addr);

	create_word(">>", v->memory[HERE], 2, 1);
	compile(v, DOCOL);
	compile(v, rsh_addr);
	compile(v, dosem_addr);

	create_word("=", v->memory[HERE], 2, 1);
	compile(v, DOCOL);
	compile(v, eq_addr);
	compile(v, dosem_addr);

	create_word("<", v->memory[HERE], 2, 1);
	compile(v, DOCOL);
	compile(v, lt_addr);
	compile(v, dosem_addr);

	/* Built-ins using the primitives */

	create_word("state", v->memory[HERE], 0, 1);
	compile(v, DOCOL);
	compile(v, dolit_addr);
	compile(v, STATE);
	compile(v, load_addr);
	compile(v, dosem_addr);

	create_word("here", v->memory[HERE], 0, 1);
	compile(v, DOCOL);
	compile(v, dolit_addr);
	compile(v, HERE);
	compile(v, load_addr);
	compile(v, dosem_addr);

	create_word(",", v->memory[HERE], 0, 0);
	compile(v, DOCOL);
	compile(v, dolit_addr);
	compile(v, HERE);
	compile(v, load_addr);
	compile(v, dolit_addr);
	compile(v, 1);
	compile(v, add_addr);
	compile(v, dolit_addr);
	compile(v, HERE);
	compile(v, store_addr);
	compile(v, dosem_addr);

	//cell allot_addr = v->memory[HERE];
	create_word("allot", v->memory[HERE], 1, 0);
	compile(v, DOCOL);
	compile(v, dolit_addr);
	compile(v, HERE);
	compile(v, load_addr);
	compile(v, add_addr);
	compile(v, dolit_addr);
	compile(v, HERE);
	compile(v, store_addr);
	compile(v, dosem_addr);

	bool newline = true;
	v->memory[STATE] = INTERACTIVE;
	char tib[WORD_SIZE + 1];
	for (;;) {
		/* Prompt the user if a newline has begun. */
		if (newline) {
			prompt(v->memory[STATE]);
		}
		/* Read a single word, or terminate the loop if EOF. */
		if (-1 == read_word(tib, sizeof(tib), &newline)) {
			break;
		}
		/* Skip whitespace. */
		if (strlen(tib) == 0) {
			continue;
		}
		cell n = atoi(tib);

		struct word *latest;
		switch (v->memory[STATE]) {
		case INTERACTIVE:
			if (0 == strcmp(tib, ":")) {
				v->memory[STATE] = COLON;
				continue;
			}

			if (n != 0 || 0 == strcmp(tib, "0")) {
				push(v, n);
				continue;
			}

			struct word *entry = lookup_word(tib);
			if (entry == NULL) {
				fprintf(stderr, "%s not found\n", tib);
				continue;
			}

			if (underflow(v, entry->inputs)) {
				fprintf(stderr, "%s requires %d inputs\n", entry->symbol, entry->inputs);
				continue;
			}

			int err = exec(v, entry->addr);
			if (err != 0)
				return err;
			break;
		case COLON:
			latest = create_word(tib, v->memory[HERE], 0, 0);
			compile(v, DOCOL);
			v->memory[STATE] = COMPILE;
			break;
		case COMPILE:
			if (0 == strcmp(tib, ";")) {
				compile(v, dosem_addr);
				v->memory[STATE] = INTERACTIVE;
				break;
			}

			if (n != 0 || 0 == strcmp(tib, "0")) {
				compile(v, dolit_addr);
				compile(v, n);
				latest->outputs += 1;
			} else if (strlen(tib) > 0) {
				struct word *entry = lookup_word(tib);
				if (entry == NULL) {
					fprintf(stderr, "%s not found\n", tib);
					continue;
				} else if (entry->flags == 1) {
					exec(v, entry->addr);
				} else {
					compile(v, entry->addr);
				}
			}
			break;
		}
	}

	return ERR_OK;
}

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	int data_size = DEFAULT_DATA_SIZE;

	argv++;
	OPT_BEGIN(argv) {
	case 'h':
		printf(NAME" [-d <cells>]\n");
		exit(0);
		continue;
	case 'd':
		data_size = atoi(OPT_ARG(argv));
		if (data_size <= 0) {
			fprintf(stderr, "-d argument must be a number greater than 0");
			exit(0);
		}
		continue;
	case '-':
		OPT_TERM(argv);
		break;
	default:
		fprintf(stderr, "Invalid option '-%c'\nTry '"NAME" -h'\n", OPT_FLAG(argv));
		exit(-1);
	} OPT_END;

	cell size = STACK_SIZE + RSTACK_SIZE + data_size;
	cell *memory = calloc(1, sizeof(*memory) * size);

	for (;;) {

		struct v64th v = {
			.sp = STACK_ADDR + STACK_SIZE,
			.rsp = RSTACK_ADDR + STACK_SIZE,
			.size = size,
			.memory = memory,
		};

		v.memory[HERE] = DATA;

		switch (run(&v)) {
		case ERR_OK:
			goto exit;
			break;
		case ERR_FAULT:
			fprintf(stderr, "invalid data address\n");
			break;
		case ERR_UNDERFLOW:
			fprintf(stderr, "stack underflow\n");
			break;
		case ERR_OVERFLOW:
			fprintf(stderr, "stack overflow\n");
			break;
		}
	}
exit:
	exit(EXIT_SUCCESS);
}
