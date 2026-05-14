# HJSON Config Primer

This document explains how to use HJSON for test configurations in web-tests. A
solid understanding of these concepts is helpful before creating or modifying
tests.

For a detailed overview of the HJSON format itself, please refer to the official
[hjson documentation](https://hjson.github.io/).

Crossbench, our test runner, extends HJSON with features for creating flexible
and reusable configurations. The two main features are Path Expansion and
Template Expansion.

# Path Expansion

Path expansion lets you embed the contents of one HJSON file into another. This
is useful for reusing common configuration blocks and avoiding duplication.

For instance, if you have a standard set of actions, you can define them in one
file:

#### `example.hjson`

```hjson
{
  web_page: https://example.com
  actions: [
    // A reusable set of actions
  ]
}
```

And then reference that file in another configuration. Crossbench will
automatically load `example.hjson` and substitute it as the value for the
`page1` field.

#### `test.hjson`

```hjson
{
  page1: example.hjson
}
```

This also works for simple values. If a string or number is used in many places,
you can place it in its own file and reference it by path.

#### `important_string.hjson`

```hjson
this is an important string that is used in many places
```

#### `use_string.hjson`

```hjson
{
  name: config that uses an important string
  important_string: important_string.hjson
}
```

# Template Expansion

While path expansion is ideal for reusing an _exact_ configuration, template
expansion is for configurations that are mostly the same but require minor
changes. Templates allow you to define a structure with placeholders that can be
filled in later.

## Basic Usage

Placeholders use the `$[ARG_NAME]` syntax.

Imagine you have a standard sequence of browser actions to run on multiple
websites. You can define the sequence once as a template:

#### `template.hjson`

```hjson
{
  actions: [
    {
      action: get
      url: $[URL]
    }
    {
      action: scroll
      ...
    },
    ...
  ]
}
```

Then, in another file, you can use this template and provide different values
for the `URL` placeholder:

#### `template-usage.hjson`

```hjson
{
  web-sites: [
    {
      template: template.hjson
      args: {
        URL: example.com
      }
    }
    {
      template: template.hjson
      args: {
        URL: google.com
      }
    }
  ]
}
```

Templates can also be defined and used within the same file, which is a useful
way to avoid repeating values.

```hjson
{
  args: {
    SCROLL_DISTANCE: 100
  }
  template: {
    actions: [
      // Scroll down
      {
        action: scroll
        distance: $[SCROLL_DISTANCE]
      }
      // Scroll up
      {
        action: scroll
        distance: -$[SCROLL_DISTANCE]
      }
    ]
  }
}
```

Arg values can be any type as long as the substitution results in a valid hjson
object.

#### `template.hjson`

```hjson
{
  string_value: This is a $[SUBSTITUTED] string
}
```

```hjson

// Valid substitution
{
  template: template.hjson
  args: {
    SUBSTITUTED: boring
  }
}

// Invalid substitutions (parsing will fail)
{
  template: template.hjson
  args: {
    SUBSTITUTED: {
      // Some dict type
    }
  }
}
{
  template: template.hjson
  args: {
    SUBSTITUTED: [
      // Some list type
    ]
  }
}
```

## Escaping Substitution

To prevent a placeholder from being replaced, use the `$[[ARG]` syntax. For
example, `name: $[[ARG]` will become `name: $[ARG]` after processing.

## List Spread Operator

You can insert the elements of a list into another list using the spread
operator (`$[...LIST_NAME]`). The placeholder must be inside a list, and the
argument provided must also be a list.

```hjson
{
  template: {
    list_item: [
      {
        action: get
        url: google.com
      }
      $[...ADDITIONAL_ACTIONS]
      {
        action: close_tab
      }
    ]
  }
  args: {
    ADDITIONAL_ACTIONS: [
      {
        action: scroll
        distance: 100
      }
    ]
  }
}
```

## Unbound Args

When nesting templates, you may need to pass some arguments to the inner
template while defining others in the outer template. Use the `unbound_args`
field to specify which arguments should be passed through without being
substituted.

For example, here is a generic template for loading and scrolling a page:

#### `scroll.hjson`

```hjson
actions: [
  {
    action: get
    url: $[URL]
  }
  {
    action: scroll
    distance: $[SCROLL_DISTANCE]
  }
]
```

We can create a more specific template that sets the scroll distance but leaves
the `URL` undefined:

#### `scroll200.hjson`

```hjson
{
  args: {
    SCROLL_DISTANCE: 200
  }
  unbound_args: [
    URL
  ]
  template: scroll.hjson
}
```

Finally, we can use the `scroll200.hjson` template and provide the `URL`:

#### `scroll-google-200.hjson`

```hjson
{
  args: {
    URL: google.com
  }
  template: scroll200.hjson
}
```

# Formatting

HJSON file formatting is managed by `hjson-js` and enforced by a presubmit check
in [`PRESUBMIT.py`](../PRESUBMIT.py). When you upload a change, the presubmit
script will automatically format your HJSON files. If your files were not
formatted correctly, you will need to amend your commit with the correctly
formatted versions.

To format files manually before uploading, run:

```bash
vpython3 tools/format_files.py <file1.hjson> <file2.hjson>
```

# Debugging Config

Large config objects can be difficult to follow and debug from source. To view
the final config object after all substitutions have been completed, run your
test with the `--debug` flag. Crossbench will include the final config objects
in its output:

```
Argument substitution resulted in the following config object:
```

# Best Practices

With path expansion and template expansion, hjson config files are very powerful
and flexible. Because of this, it is very easy to create config files that are
unreadable and difficult to follow.

Here are a few best practices to help avoid common pitfalls.

1.  **Avoid Duplication:** If you repeat the same list of actions or
    configuration block, move it to a separate file and use path expansion.

2.  **Prefer Inlining Simple Configs:** For configurations used only once, keep
    them within a single file. Creating many small files for non-reusable blocks
    can make tests harder to read. If you want to repeat a list of actions
    multiple times, you can use inline template expansion using the list spread
    operator:

```hjson
{
  template: [
    $[...REPEATED_ACTIONS]
    $[...REPEATED_ACTIONS]
    $[...REPEATED_ACTIONS]
  ]
  args: {
    REPEATED_ACTIONS: [
      {
        action: scroll
        distance: 100
      }
      {
        action: scroll
        distance: -100
      }
    ]
  }
}
```

3.  **Parameterize When Necessary, Not Before:** Only create a template when you
    have an immediate need to reuse a configuration with different parameters.
    Avoid premature templating.

4.  **Keep Code in Separate Files:** For multi-line JavaScript or SQL snippets,
    place them in `.js` or `.sql` files and reference them from your HJSON. This
    ensures they are properly formatted and linted by our presubmit checks.
