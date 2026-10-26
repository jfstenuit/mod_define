# mod_define

Late-bound configuration variables for the Apache HTTP Server.

mod_define lets you define variables in the httpd configuration and use them
in the arguments of any directive. Unlike the `Define` directive that httpd
2.4 has in its core, mod_define substitutes the variables once the whole
configuration has been read: in the `pre_config` hook, it walks the parsed
configuration tree and replaces every reference in place.

## Why mod_define is still useful with httpd 2.4

Since httpd 2.4, core `Define` and `${name}` cover the simple case: a value
defined once, near the top, and used further down. mod_define therefore looks
obsolete. It is not, as we found when porting a large production
configuration from mod_define to core `Define`. The two mechanisms differ in
when they bind a value, and that difference decides how a layered
configuration can be written.

Behaviour of core `Define`:

| Aspect | Core `Define` (httpd 2.4) | mod_define |
|---|---|---|
| When a value is substituted | As each line is read. A later `Define` does not change lines already read | After the whole configuration has been read |
| Overriding a default | The override must come before every use of the value, so includes must be ordered around the values | Defaults first, overrides last: the last value applies everywhere, including lines read before it |
| Empty value | `Define x ""` defines `x` without a value: `${x}` is left as it is, with warning AH00111 | Empty values are possible (in this repository, through `${mod_define::empty}`) |
| Values passed to mod_macro | `${x}` is substituted before `Use` splits its arguments: a value that contains spaces becomes several macro arguments | `Use` splits its arguments first: the reference stays one argument and is resolved later |
| Values in macro expansion | The value itself is copied into every expanded line | Only the short reference is copied, so expanded lines stay small until substitution |

In practice, a layered configuration written for mod_define cannot be moved
to core `Define` by renaming the syntax.

Use core `Define` for switches tested by `<IfDefine>` and for values that are
defined once before their use. Use mod_define when values are set in layers
or inside macros, or when a late override must apply to the whole
configuration.

Late binding has a cost too: a variable has one value per scope for the whole
configuration (it cannot differ per `<VirtualHost>`), and mistakes appear
only when the configuration is checked as a whole.

## Status of this repository

This repository holds the 2.x code base, written for httpd 2.0 and 2.2, plus
one later patch. It has not yet been tested against httpd 2.4. As it stands:

- The directive is named `Define` and references use `$name` and `${name}`,
  the same name and syntax as core `Define` in httpd 2.4. Core resolves
  `${name}` as each line is read, and warns (AH00111) about every name it does
  not know.
- Variables are scoped per file (version 2.2): a variable defined in one file
  is not visible in another, and a reference falls back to the process
  environment. Layering defaults and overrides across files therefore does
  not work in this version.
- `${mod_define::empty}` expands to an empty string, as a workaround for the
  lack of empty values.
- The meta characters can be changed by defining `mod_define::escape`,
  `mod_define::dollar`, `mod_define::braceopen` and `mod_define::braceclose`.

## Next: our version for httpd 2.4

We are writing our own version of the module for httpd 2.4. The goals, to be
refined as the work progresses:

- build against current httpd 2.4 (`apxs`, Debian `apache2-dev`);
- a directive name and reference syntax that do not clash with core `Define`;
- empty values as a normal case;
- the scope of variables (global with last value wins, or per file) as an
  explicit design choice;
- protection against recursive definitions and over-long lines.

## Building and installation

The module is built with `apxs`, which comes with httpd (on Debian, in
`apache2-dev`):

```
apxs -o mod_define.so -c mod_define.c
```

Then load it in the httpd configuration:

```
LoadModule define_module modules/mod_define.so
```

Adjust the path if the module is installed elsewhere.

## History

| Version | Date | Authors | Change |
|---|---|---|---|
| 1.0 | December 1998 | Ralf S. Engelschall, Christian Reiber | Original module for httpd 1.3 |
| 1.1 | August 1999 | Ralf S. Engelschall | Complete overhaul |
| 2.0 | August 2006 | Rainer Jung | Port to httpd 2.0 and 2.2, published at <http://people.apache.org/~rjung/mod_define/> |
| 2.1 | October 2006 | Rainer Jung | Minor fixes |
| 2.2 | February 2011 | W. Kleedorfer | File scope for variables, published at <http://code.google.com/p/mod-define/> |
| n.a. | September 2011 | Russell Davis | `mod_define::empty` variable |

The original plain-text documentation is in [README](README).

## Licence

Apache License, Version 2.0: see [LICENSE.md](LICENSE.md). The
[NOTICE](NOTICE) file carries the attribution and the licence of the original
httpd 1.3 module by Ralf S. Engelschall and Christian Reiber; keep it with
any redistribution.
