#include "dnsinfo.h"
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <resolv.h>

#define RESOLV_CONF_SEPARATORS " \t\r\n"

/**
 * This is a very simple parser of /etc/resolv.conf.
 * It is not intended to be feature complete.
 * It only serves as an interim solution until we have XPC and configd.
 *
 * The file is reported as the single default resolver. Recognized
 * keywords are nameserver (IPv4 and IPv6 addresses), domain, search,
 * port, timeout and options; anything else, including sortlist, is
 * ignored. As in resolv.conf(5), a later domain or search line replaces
 * the search list of an earlier one.
 */

const char *
dns_configuration_notify_key    ()
{
	// the key Apple's dnsinfo/dnsinfo_copy.c returns; nothing posts it yet,
	// but callers need a valid name to register for notify(3) changes
	return "com.apple.system.SystemConfiguration.dns_configuration";
}

static int add_nameserver(dns_resolver_t* resolver, const char* value)
{
	struct sockaddr_storage address;
	struct sockaddr** list;
	struct sockaddr* copy;
	size_t length;

	memset(&address, 0, sizeof(address));

	if (strchr(value, ':') != NULL)
	{
		struct sockaddr_in6* a6 = (struct sockaddr_in6*) &address;

		length = sizeof(*a6);
		a6->sin6_len = length;
		a6->sin6_family = AF_INET6;
		a6->sin6_port = htons(NS_DEFAULTPORT);

		if (inet_pton(AF_INET6, value, &a6->sin6_addr) != 1)
			return 0;
	}
	else
	{
		struct sockaddr_in* a4 = (struct sockaddr_in*) &address;

		length = sizeof(*a4);
		a4->sin_len = length;
		a4->sin_family = AF_INET;
		a4->sin_port = htons(NS_DEFAULTPORT);

		if (inet_pton(AF_INET, value, &a4->sin_addr) != 1)
			return 0;
	}

	list = (struct sockaddr**) realloc(resolver->nameserver,
			sizeof(*list) * (resolver->n_nameserver + 1));
	if (list == NULL)
		return -1;
	resolver->nameserver = list;

	copy = (struct sockaddr*) malloc(length);
	if (copy == NULL)
		return -1;

	memcpy(copy, &address, length);
	list[resolver->n_nameserver++] = copy;
	return 0;
}

static void clear_search(dns_resolver_t* resolver)
{
	while (resolver->n_search > 0)
		free(resolver->search[--resolver->n_search]);

	free(resolver->search);
	resolver->search = NULL;
}

static int add_search(dns_resolver_t* resolver, const char* value)
{
	char** list;
	char* copy;

	list = (char**) realloc(resolver->search,
			sizeof(*list) * (resolver->n_search + 1));
	if (list == NULL)
		return -1;
	resolver->search = list;

	copy = strdup(value);
	if (copy == NULL)
		return -1;

	list[resolver->n_search++] = copy;
	return 0;
}

static int replace_string(char** field, const char* value)
{
	char* copy = strdup(value);

	if (copy == NULL)
		return -1;

	free(*field);
	*field = copy;
	return 0;
}

// Returns -1 on allocation failure, 0 otherwise.
static int parse_line(dns_resolver_t* resolver, char* line)
{
	char* save = NULL;
	char* key;
	char* value;
	char* comment;

	comment = strpbrk(line, "#;");
	if (comment != NULL)
		*comment = '\0';

	key = strtok_r(line, RESOLV_CONF_SEPARATORS, &save);
	if (key == NULL)
		return 0;

	value = strtok_r(NULL, RESOLV_CONF_SEPARATORS, &save);
	if (value == NULL)
		return 0;

	if (strcmp(key, "nameserver") == 0)
	{
		return add_nameserver(resolver, value);
	}
	else if (strcmp(key, "domain") == 0)
	{
		clear_search(resolver);
		return replace_string(&resolver->domain, value);
	}
	else if (strcmp(key, "search") == 0)
	{
		clear_search(resolver);
		do
		{
			if (add_search(resolver, value) != 0)
				return -1;
		}
		while ((value = strtok_r(NULL, RESOLV_CONF_SEPARATORS, &save)) != NULL);
	}
	else if (strcmp(key, "port") == 0)
	{
		resolver->port = (uint16_t) atoi(value);
	}
	else if (strcmp(key, "timeout") == 0)
	{
		int timeout = atoi(value);

		if (timeout > RES_MAXRETRANS)
			timeout = RES_MAXRETRANS;
		if (timeout > 0)
			resolver->timeout = timeout;
	}
	else if (strcmp(key, "options") == 0)
	{
		// Options accumulate over all options lines, as in res_init;
		// keep them as one space-separated list.
		char* options = resolver->options;
		size_t length = (options != NULL) ? strlen(options) : 0;
		char* word;

		for (word = value; word != NULL; word = strtok_r(NULL, RESOLV_CONF_SEPARATORS, &save))
		{
			size_t wlen = strlen(word);
			char* grown = (char*) realloc(options, length + wlen + 2);

			if (grown == NULL)
				return -1;
			options = grown;
			resolver->options = options;

			if (length > 0)
				options[length++] = ' ';
			memcpy(options + length, word, wlen + 1);
			length += wlen;
		}
	}

	return 0;
}

dns_config_t *
dns_configuration_copy		()
{
	dns_config_t* config;
	dns_resolver_t* resolver;
	FILE* file;
	char buf[1024];
	int failed = 0;

	file = fopen(_PATH_RESCONF, "r");
	if (file == NULL)
		return NULL;

	config = (dns_config_t*) calloc(1, sizeof(*config));
	if (config == NULL)
	{
		fclose(file);
		return NULL;
	}

	config->version = DNSINFO_VERSION;
	config->resolver = (dns_resolver_t**) calloc(1, sizeof(*config->resolver));
	resolver = (dns_resolver_t*) calloc(1, sizeof(*resolver));
	if (config->resolver == NULL || resolver == NULL)
	{
		free(resolver);
		fclose(file);
		dns_configuration_free(config);
		return NULL;
	}

	config->resolver[0] = resolver;
	config->n_resolver = 1;

	resolver->flags = DNS_RESOLVER_FLAGS_REQUEST_ALL_RECORDS;
	resolver->search_order = DEFAULT_SEARCH_ORDER;

	while (!failed && fgets(buf, sizeof(buf), file) != NULL)
	{
		if (parse_line(resolver, buf) != 0)
			failed = 1;
	}

	if (ferror(file))
		failed = 1;
	fclose(file);

	// Without a name server there is no usable configuration; callers
	// such as res_init then fall back to their own defaults.
	if (failed || resolver->n_nameserver == 0)
	{
		dns_configuration_free(config);
		return NULL;
	}

	return config;
}

void
dns_configuration_free		(dns_config_t	*config)
{
	int i, j;

	if (config == NULL)
		return;

	for (i = 0; i < config->n_resolver; i++)
	{
		dns_resolver_t* resolver = config->resolver[i];

		if (resolver == NULL)
			continue;

		free(resolver->domain);
		free(resolver->options);

		clear_search(resolver);

		for (j = 0; j < resolver->n_nameserver; j++)
			free(resolver->nameserver[j]);
		free(resolver->nameserver);

		free(resolver);
	}

	free(config->resolver);
	free(config);
}

void
_dns_configuration_ack		(dns_config_t	*config,
				 const char	*bundle_id)
{
}
