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
| Empty value | `Define x ""` defines `x` without a value: `${x}` is left as it is, with warning AH00111 | `DefineStr x ""` and `DefineStr x` both set an empty value |
| Values passed to mod_macro | `${x}` is substituted before `Use` splits its arguments: a value that contains spaces becomes several macro arguments | `Use` splits its arguments first: the reference stays one argument and is resolved later |
| Values in macro expansion | The value itself is copied into every expanded line | Only the short reference is copied, so expanded lines stay small until substitution |

In practice, a layered configuration written for mod_define cannot be moved
to core `Define` by renaming the syntax.

Use core `Define` for switches tested by `<IfDefine>` and for values that are
defined once before their use. Use mod_define when values are set in layers
or inside macros, or when a late override must apply to the whole
configuration.

Late binding has a cost too: a variable has one value for the whole
configuration (it cannot differ per `<VirtualHost>`), and mistakes appear
only when the configuration is checked as a whole.

## Usage

Version 4.0 targets httpd 2.4. Load the module before the first `DefineStr`:

```
LoadModule define_module modules/mod_define.so

DefineStr backend "http://~{host}:8080"
ProxyPass "/app" "~{backend}/app"

# A later value applies everywhere, including the lines above
DefineStr host "127.0.0.1"
```

### Directives

| Directive | Default | Description |
|---|---|---|
| `DefineStr name [value]` | | Sets a variable. Without a value, or with `""`, the value is empty. The value is stored as it is: references in it are expanded where the variable is used |
| `DefineMaxRecursion n` | 200 | Maximum number of nested variables (a value that refers to a variable whose value refers to another, and so on) |
| `DefineMaxString n` | 16000 | Maximum length of the arguments of a directive after substitution |
| `DefineMetaDelim c` | `~` | Character that starts a reference |
| `DefineMetaBraceOpen c` | `{` | Opening brace of a reference |
| `DefineMetaBraceClose c` | `}` | Closing brace of a reference |
| `DefineMetaEscape c` | `\` | Escape character |

All are server configuration directives. They are run while httpd reads the
configuration, so they take effect wherever they appear, including inside
macros and sections; the last setting wins.

### References

- A reference is `~{name}`. Names are made of letters, digits, `_`, `:` and
  `-`, and are not case-sensitive.
- Variables are global: the last value set anywhere in the configuration
  applies to every reference, including references read before it.
- A `~` that is not followed by `{` is ordinary text (for example `~[` or
  `~/` in a regular expression).
- The escape character protects the character that follows it: `\~{name}` is
  not substituted. Both characters stay in the text, which keeps `\~` valid
  in a regular expression.
- References are substituted in the arguments of every directive, sections
  included (`<VirtualHost ~{ip}:443>`). Directives that httpd runs while it
  reads the configuration, such as `Include`, `LoadModule`, `<IfDefine>` and
  `<IfModule>`, see the reference as text, not its value.
- With mod_macro, a reference passed as an argument of `Use` is copied into
  the expanded lines and substituted there.
- Core `Define` and `${name}` keep working alongside: httpd resolves
  `${name}` as it reads each line, mod_define resolves `~{name}` afterwards.

### Errors

- An undefined variable, an illegal character in a reference, an empty
  reference (`~{}`) or a missing closing brace is reported as an error, with
  the file and line, and the reference is left as it is. httpd still
  starts.
- A variable that refers to itself, directly or through other variables,
  more nested variables than `DefineMaxRecursion`, a line longer than
  `DefineMaxString`, an illegal character in a `DefineStr` name and meta
  characters that are not all different stop httpd. For the first two, the
  message shows the chain of variables, for example
  `variable 'a' refers to itself: a -> b -> a`.

### Feature tests

When the module is loaded, it defines `MODDEFINE_EmptyValue`,
`MODDEFINE_MaxRecursion` and `MODDEFINE_MaxString`, which configurations can
test with `<IfDefine>`.

### Changes from version 2.x

Version 4.0 is not compatible with the 2.x configuration syntax:

| 2.x | 4.0 |
|---|---|
| `Define name value` | `DefineStr name value` (core httpd 2.4 now owns `Define`) |
| `${name}` and `$name` | `~{name}` only (core httpd 2.4 now owns `${name}`) |
| Variables visible only in the file that defines them | Global variables, last value wins |
| Falls back to the process environment | No fallback; use core `${name}` for environment variables |
| `${mod_define::empty}` | `DefineStr name ""` |
| `Define mod_define::escape` and similar | `DefineMetaEscape` and the other `DefineMeta*` directives |

## Building and testing

The module is built with `apxs`, which comes with httpd (on Debian, in
`apache2-dev`):

```
make          # apxs -c mod_define.c, result in .libs/mod_define.so
make test     # configuration tests with httpd -t, nothing is installed
```

The tests need httpd 2.4 with `mod_info` and `mod_macro` (on Debian,
`apache2`). To run httpd under a tool such as valgrind:

```
WRAPPER="valgrind -q --error-exitcode=99" make test
```

Install the module with your platform's packaging, or copy
`.libs/mod_define.so` to the modules directory of httpd, then load it:

```
LoadModule define_module modules/mod_define.so
```

## History

| Version | Date | Authors | Change |
|---|---|---|---|
| 1.0 | December 1998 | Ralf S. Engelschall, Christian Reiber | Original module for httpd 1.3 |
| 1.1 | August 1999 | Ralf S. Engelschall | Complete overhaul |
| 2.0 | August 2006 | Rainer Jung | Port to httpd 2.0 and 2.2, published at <http://people.apache.org/~rjung/mod_define/> |
| 2.1 | October 2006 | Rainer Jung | Minor fixes |
| 2.2 | February 2011 | W. Kleedorfer | File scope for variables, published at <http://code.google.com/p/mod-define/> |
| n.a. | September 2011 | Russell Davis | `mod_define::empty` variable |
| 4.0 | October 2026 | Jean-Francois Stenuit | Rewritten for httpd 2.4: `DefineStr` and `~{name}`, global variables, empty values, limits on nesting and line length |

The original plain-text documentation is in [README](README).

## Licence

Apache License, Version 2.0: see [LICENSE.md](LICENSE.md). The
[NOTICE](NOTICE) file carries the attribution and the licence of the original
httpd 1.3 module by Ralf S. Engelschall and Christian Reiber; keep it with
any redistribution.
