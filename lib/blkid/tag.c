/*
 * tag.c - allocation/initialization/free routines for tag structs
 *
 * Copyright (C) 2001 Andreas Dilger
 * Copyright (C) 2003 Theodore Ts'o
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 * %End-Header%
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "blkidP.h"

#ifdef DEBUG_TAG
#define DBG(x)	x
#else
#define DBG(x)
#endif

static blkid_tag blkid_new_tag(void)
{
	blkid_tag tag;

	if (!(tag = (blkid_tag) calloc(1, sizeof(struct blkid_struct_tag))))
		return NULL;

	INIT_LIST_HEAD(&tag->bit_tags);
	INIT_LIST_HEAD(&tag->bit_names);

	return tag;
}

void blkid_free_tag(blkid_tag tag)
{
	if (!tag)
		return;

	DBG(printf("    freeing tag %s=%s\n", tag->bit_name,
		   tag->bit_val ? tag->bit_val : "(NULL)"));
	DEB_DUMP_TAG(tag);

	list_del(&tag->bit_tags);	/* list of tags for this device */
	list_del(&tag->bit_names);	/* list of tags with this type */

	if (tag->bit_name)
		free(tag->bit_name);
	if (tag->bit_val)
		free(tag->bit_val);

	free(tag);
}

/*
 * Find the desired tag on a device.  If value is NULL, then the
 * first such tag is returned, otherwise return only exact tag if found.
 */
blkid_tag blkid_find_tag_dev(blkid_dev dev, const char *type)
{
	struct list_head *p;

	if (!dev || !type)
		return NULL;

	list_for_each(p, &dev->bid_tags) {
		blkid_tag tmp = list_entry(p, struct blkid_struct_tag,
					   bit_tags);

		if (!strcmp(tmp->bit_name, type))
			return tmp;
	}
	return NULL;
}

/*
 * Find the desired tag type in the cache.
 * We return the head tag for this tag type.
 */
static blkid_tag blkid_find_head_cache(blkid_cache cache, const char *type)
{
	blkid_tag head = NULL, tmp;
	struct list_head *p;

	if (!cache || !type)
		return NULL;

	list_for_each(p, &cache->bic_tags) {
		tmp = list_entry(p, struct blkid_struct_tag, bit_tags);
		if (!strcmp(tmp->bit_name, type)) {
			DBG(printf("    found cache tag head %s\n", type));
			head = tmp;
			break;
		}
	}
	return head;
}

/*
 * Set a tag on an existing device.
 * 
 * If replace is non-zero, blkid_set_tag() will replace the existing
 * tag with the specified value.  Otherwise, it will add the specified
 * tag to the device.
 *
 * If value is NULL, then delete all tags with that name from the
 * device.
 */
int blkid_set_tag(blkid_dev dev, const char *name,
		  const char *value, const int vlength, int replace)
{
	blkid_tag	t = 0, head = 0;
	char		*val = 0;

	if (!dev || !name)
		return -BLKID_ERR_PARAM;

repeat:
	t = blkid_find_tag_dev(dev, name);
	val = blkid_strndup(value, vlength);
	if (!value) {
		if (t) {
			blkid_free_tag(t);
			goto repeat;
		} else
			goto link_tags;
	}
	if (!val)
		goto errout;
	if (t) {
		if (!strcmp(t->bit_val, val)) {
			/* Same thing, exit */
			free(val);
			return 0;
		}
		if (replace) {
			free(t->bit_val);
			t->bit_val = val;
			goto link_tags;
		}
		dev->bid_flags |= BLKID_BID_FL_MTYPE;
	}

	/* Existing tag not present, add to device */
	t = blkid_new_tag();
	if (!t)
		goto errout;
	t->bit_name = blkid_strdup(name);
	t->bit_val = val;
	t->bit_dev = dev;

	list_add_tail(&t->bit_tags, &dev->bid_tags);
		
	if (dev->bid_cache) {
		head = blkid_find_head_cache(dev->bid_cache, t->bit_name);
		if (!head) {
			head = blkid_new_tag();
			if (!head)
				goto errout;

			DBG(printf("    creating new cache tag head %s\n",
				   name));
			head->bit_name = blkid_strdup(name);
			if (!head->bit_name)
				goto errout;
			list_add_tail(&head->bit_tags,
				      &dev->bid_cache->bic_tags);
		}
		list_add_tail(&t->bit_names, &head->bit_names);
	}
	
link_tags:
	/* Link common tags directly to the device struct */
	if (!strcmp(name, "TYPE") && (!val || !dev->bid_type))
		dev->bid_type = val;
	else if (!strcmp(name, "LABEL"))
		dev->bid_label = val;
	else if (!strcmp(name, "UUID"))
		dev->bid_uuid = val;
		
	if (dev->bid_cache)
		dev->bid_cache->bic_flags |= BLKID_BIC_FL_CHANGED;
	return 0;

errout:
	if (t)
		blkid_free_tag(t);
	else if (val)
		free(val);
	if (head)
		blkid_free_tag(head);
	return -BLKID_ERR_MEM;
}


/*
 * Parse a "NAME=value" string.  This is slightly different than
 * parse_token, because that will end an unquoted value at a space, while
 * this will assume that an unquoted value is the rest of the token (e.g.
 * if we are passed an already quoted string from the command-line we don't
 * have to both quote and escape quote so that the quotes make it to
 * us).
 *
 * Returns 0 on success, and -1 on failure.
 */
int blkid_parse_tag_string(const char *token, char **ret_type, char **ret_val)
{
	char *name, *value, *cp;

	DBG(printf("trying to parse '%s' as a tag\n", token));

	if (!token || !(cp = strchr(token, '=')))
		return -1;

	name = blkid_strdup(token);
	if (!name)
		return -1;
	value = name + (cp - token);
	*value++ = '\0';
	if (*value == '"' || *value == '\'') {
		char c = *value++;
		if (!(cp = strrchr(value, c)))
			goto errout; /* missing closing quote */
		*cp = '\0';
	}
	value = blkid_strdup(value);
	if (!value)
		goto errout;

	*ret_type = name;
	*ret_val = value;

	return 0;

errout:
	free(name);
	return -1;
}

/*
 * Tag iteration routines for the public libblkid interface.
 *
 * These routines do not expose the list.h implementation, which are a
 * contamination of the namespace, and which force us to reveal far, far
 * too much of our internal implemenation.  I'm not convinced I want
 * to keep list.h in the long term, anyway.  It's fine for kernel
 * programming, but performance is not the #1 priority for this
 * library, and I really don't like the tradeoff of type-safety for
 * performance for this application.  [tytso:20030125.2007EST]
 */

/*
 * This series of functions iterate over all tags in a device
 */
#define TAG_ITERATE_MAGIC	0x01a5284c
	
struct blkid_struct_tag_iterate {
	int			magic;
	blkid_dev		dev;
	struct list_head	*p;
};

extern blkid_tag_iterate blkid_tag_iterate_begin(blkid_dev dev)
{
	blkid_tag_iterate	iter;

	iter = malloc(sizeof(struct blkid_struct_tag_iterate));
	if (iter) {
		iter->magic = TAG_ITERATE_MAGIC;
		iter->dev = dev;
		iter->p	= dev->bid_tags.next;
	}
	return (iter);
}

/*
 * Return 0 on success, -1 on error
 */
extern int blkid_tag_next(blkid_tag_iterate iter,
			  const char **type, const char **value)
{
	blkid_tag tag;
	
	*type = 0;
	*value = 0;
	if (!iter || iter->magic != TAG_ITERATE_MAGIC ||
	    iter->p == &iter->dev->bid_tags)
		return -1;
	tag = list_entry(iter->p, struct blkid_struct_tag, bit_tags);
	*type = tag->bit_name;
	*value = tag->bit_val;
	iter->p = iter->p->next;
	return 0;
}

extern void blkid_tag_iterate_end(blkid_tag_iterate iter)
{
	if (!iter || iter->magic != TAG_ITERATE_MAGIC)
		return;
	iter->magic = 0;
	free(iter);
}

/*
 * This function returns a device which matches a particular
 * type/value pair.  If there is more than one device that matches the
 * search specification, it returns the one with the highest priority
 * value.  This allows us to give preference to EVMS or LVM devices.
 *
 * XXX there should also be an interface which uses an iterator so we
 * can get all of the devices which match a type/value search parameter.
 */
extern blkid_dev blkid_find_dev_with_tag(blkid_cache cache,
					 const char *type,
					 const char *value)
{
	blkid_tag	head, found;
	blkid_dev	dev;
	int		pri;
	struct list_head *p;

	if (!cache || !type || !value)
		return NULL;

	DBG(printf("looking for %s=%s in cache\n", type, value));
	
try_again:
	pri = -1;
	found = 0;
	head = blkid_find_head_cache(cache, type);

	if (head) {
		list_for_each(p, &head->bit_names) {
			blkid_tag tmp = list_entry(p, struct blkid_struct_tag, 
						   bit_names);

			if (!strcmp(tmp->bit_val, value) &&
			    tmp->bit_dev->bid_pri > pri) {
				found = tmp;
				dev = found->bit_dev;
				pri = dev->bid_pri;
			}
		}
	}
	dev = blkid_verify_devname(cache, dev);
	if (dev && strcmp(found->bit_val, value))
		dev = 0;

	if ((!head || !dev) && !(cache->bic_flags & BLKID_BIC_FL_PROBED)) {
		blkid_probe_all(cache);
		goto try_again;
	}
	return dev;
}
