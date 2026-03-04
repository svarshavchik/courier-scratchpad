/*
** Copyright 1998 - 2000 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"courier.h"
#include	"rw.h"
#include	"rfc822/rfc822.h"
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

namespace {

	struct wrapped_header_iter {
		using iterator_category=std::output_iterator_tag;
		using value_type=void;
		using pointer=void;
		using reference=void;
		using difference_type=void;

		std::string wrapped_header;

		const char *prefix="";

		auto &operator*() { return *this; }

		auto &operator++() { return *this; }

		auto &operator++(int) { return *this; }

		auto &operator=(std::string l)
		{
			while (!l.empty() && isspace(l.back()))
				l.pop_back();
			wrapped_header += prefix;
			wrapped_header += std::move(l);
			prefix="\n  ";
			return *this;
		}
	};
}

char *rw_rewrite_header(
	struct rw_transport *rw,
	std::string_view header, int mode, const rfc822::tokens &sender,
	const rfc822::tokens &host,
	char **errmsgptr)
{
	std::vector<char *> bufptrs;
	char	*new_header=0;

	rfc822::tokens rfcp{header};
	rfc822::addresses rfca{rfcp};

	bufptrs.reserve(rfca.size());

	*errmsgptr=0;

	for (auto &a:rfca)
	{
		struct	rw_info_rewrite rwr;
		struct	rw_info rwi{mode, sender, host};

		if (a.address.empty())
			continue;

		rwr.buf=0;
		rwr.errmsg=0;

		rw_info_init(&rwi, a.address, rw_err_func);
		rwi.udata=(void *)&rwr;

		/*
		** If address is of the form "@", this is a delivery
		** status notification, so leave it untouched.
		*/

		if (rwi.addr.size() == 1 && rwi.addr[0].type == '@')
			rw_rewrite_print(&rwi);

		/* --- */

		else rw_rewrite_module(rw, &rwi,
				       rw_rewrite_print
		);

		if ( (*errmsgptr=((struct rw_info_rewrite *)rwi.udata)->errmsg)
			!= 0)	break;

		auto bufptr=((struct rw_info_rewrite *)rwi.udata)->buf;

		if (!bufptr)
			continue;

		bufptrs.push_back(bufptr);

		a.address.clear();

		a.address.push_back( {0, bufptr} );
	}

	new_header=0;

	if ( !*errmsgptr)
	{
		wrapped_header_iter collect;

		rfca.print_wrapped(rfca.begin(), rfca.end(), 70, collect);

		new_header=strdup(collect.wrapped_header.c_str());
	}

	for (auto &b:bufptrs)
		if (b) free(b);

	return (new_header);
}
