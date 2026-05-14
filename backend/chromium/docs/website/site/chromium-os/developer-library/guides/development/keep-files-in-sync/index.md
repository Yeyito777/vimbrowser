---
breadcrumbs:
- - /chromium-os/developer-library/guides
  - ChromiumOS > Guides
page_name: keep-files-in-sync
title: How to use Gerrit IfThisThenThat Lint to keep files in sync
---

<!--
NB: Changing this file will likely trigger ITTT errors in Gerrit since it tries
to parse the examples & text as if they were real directives. You can ignore it
or use NO_IFTTT=examples in the commit message, or utilize some of the markdown
trickery below like empty HTML comments that are elided when rendered.
-->

IfThisThenThat helps enforce that **if** some code gets changed in one place,
**then** it is also changed in some other place. It is a linter that generates
findings in Gerrit when code in the user-defined source block has changed but
the corresponding target file or block has not.

It is most useful when code crosses language, directory or team boundaries and
should not replace testing or basic DRY principles.

[TOC]

## Syntax

<!-- NB: The inline comments disable the linter on the lines. -->

Add `// LINT`<!-- -->`.IfChange` before the content you want to change.

Add `// LINT`<!-- -->`.ThenChange(path)` after the content you want to change.

Some other syntax:

```
// LINT.IfChange
... content
// LINT.ThenChange(path)

// LINT.IfChange
... content
// LINT.ThenChange(path:label)

// LINT.IfChange(my_own_label)
... content
// LINT.ThenChange(path)

// LINT.IfChange(my_other_label)
... content
// LINT.ThenChange(path:label)
```

## Example

*   <https://source.chromium.org/chromium/chromium/src/+/20480c971bc02993918999e1fc784c02912cb12a:ash/public/cpp/accelerator_actions.h;l=32>

## Ignoring the linter

Add a line to your commit message in the format:

```
NO_IFTTT=<reason>
```

## References

*   [Gerrit IfThisThenThat Lint](http://go/gerrit-ifthisthenthat) (Googler-only)
