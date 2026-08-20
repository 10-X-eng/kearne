# Kearne themes

A theme is a UTF-8 YAML file with schema `kearne.theme/v1`, a stable lowercase ID, a display name, `light` or `dark` appearance, and a `tokens` map.

Extend `light` or `dark` to override selected tokens:

```yaml
schema: kearne.theme/v1
id: studio.high-contrast
name: Studio High Contrast
appearance: dark
extends: dark
tokens:
  accent: "#58c7e8"
  focus: "#79d9f2"
  fontDataFamily: JetBrains Mono
```

Omit `extends` only when defining every token. The built-in [light theme](light.yml) is the complete reference. Import validates the schema, IDs, keys, scalar types, numeric ranges, opacity, file size, and essential contrast pairs before copying the file into the user theme directory. Unknown or duplicate keys are errors.
