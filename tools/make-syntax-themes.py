#!/usr/bin/env python3
"""Generates Kode's syntax highlighting themes from Qt Creator's color schemes.

    tools/make-syntax-themes.py [QT_CREATOR_STYLES_DIR]

Writes assets/themes/kode-light.theme and kode-dark.theme (KSyntaxHighlighting
themes) from Qt Creator's "Light (2024)" and "Dark (2024)" schemes
(light-2024.xml, dark-2024.xml), by default from
/opt/Qt/Qt Creator.app/Contents/Resources/styles. Never edit the themes by
hand: change this script and run it again.

Qt Creator fills the categories a scheme leaves out with its built-in defaults
(FontSettings::initialFormats() in src/plugins/texteditor/fontsettings.cpp,
DEFAULTS below), and maps KSyntaxHighlighting's text styles to its categories
in categoryForTextStyle() (src/plugins/texteditor/highlighter.cpp, STYLES
below). This does the same. Departures: an #include's file (Import) is a string,
as Qt Creator's C++ editor draws it; and a scheme without "Enumeration" (the
dark one) would give constants the default dark magenta, unreadable on a dark
background; they take the scheme's number color instead.

Semantic highlighting (a language server's names: types, fields, locals…)
takes Qt Creator's categories into "semantic-styles", which KSyntaxHighlighting
ignores and Kode reads: the scheme's, else Qt Creator's defaults
(SEMANTIC_DEFAULTS, from initialFormats()) with its fallbacks (Namespace from
Type, Macro from Function). Declaration, FunctionDefinition and
OutputArgument are mixins over a category, as in Qt Creator.

Kode draws plain text in the system's text color: "Normal" carries the
scheme's color only so the theme is complete; the editor does not use it.
"""

import json
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_STYLES = Path("/opt/Qt/Qt Creator.app/Contents/Resources/styles")

# Qt Creator's defaults (fontsettings.cpp): Qt::darkBlue, Qt::darkGreen, ...
DEFAULTS = {
    "Text": {"foreground": "#000000", "background": "#ffffff"},
    "Number": {"foreground": "#000080"},
    "String": {"foreground": "#008000"},
    "PrimitiveType": {"foreground": "#808000"},
    "Type": {"foreground": "#800080"},
    "Local": {"foreground": "#092e64"},
    "Field": {"foreground": "#800000"},
    "Global": {"foreground": "#ce5c00"},
    "Enumeration": {"foreground": "#800080"},
    "Function": {"foreground": "#00677c"},
    "Keyword": {"foreground": "#808000"},
    "Operator": {},
    "Preprocessor": {"foreground": "#000080"},
    "Macro": {"foreground": "#00677c"},
    "Label": {"foreground": "#800000"},
    "Attribute": {"foreground": "#808000"},
    "Comment": {"foreground": "#008000"},
    "Doxygen.Comment": {"foreground": "#000080"},
    "Doxygen.Tag": {"foreground": "#0000ff"},
    "Info": {},
    "Warning": {"underlineColor": "#ffbe00"},
    "ErrorContext": {"underlineColor": "#ff0000"},
    "Error": {"underlineColor": "#ff0000"},
}

# Qt Creator's defaults for the semantic categories (fontsettings.cpp),
# as a scheme writes them.
SEMANTIC_DEFAULTS = {
    "Type": {"foreground": "#800080"},
    "Namespace": {"foreground": "#008000"},
    "Local": {"foreground": "#092e64"},
    "Parameter": {"foreground": "#092e64"},
    "Field": {"foreground": "#800000"},
    "Global": {"foreground": "#ce5c00"},
    "Enumeration": {"foreground": "#800080"},
    "Function": {"foreground": "#00677c"},
    "VirtualMethod": {"foreground": "#00677c", "italic": "true"},
    "Static": {},
    "Macro": {"foreground": "#00677c"},
    "Label": {"foreground": "#800000"},
    "QmlTypeId": {"foreground": "#800080"},
    "QmlLocalId": {"italic": "true"},
    "QmlExternalId": {"foreground": "#000080", "italic": "true"},
    "QmlRootObjectProperty": {"italic": "true"},
    "QmlScopeObjectProperty": {"italic": "true"},
    "QmlExternalObjectProperty": {"foreground": "#000080", "italic": "true"},
    "QmlStateName": {"italic": "true"},
    "JsScopeVar": {"foreground": "#2985c7", "italic": "true"},
    "JsImportVar": {"foreground": "#0055af", "italic": "true"},
    "JsGlobalVar": {"foreground": "#0055af", "italic": "true"},
    "Declaration": {"bold": "true"},
    "FunctionDefinition": {},
    "OutputArgument": {"italic": "true"},
}

# Categories a scheme without them takes from another of its own (fontsettings.cpp).
SEMANTIC_FALLBACKS = {"Namespace": "Type", "Macro": "Function", "Enumeration": "Number"}

# KSyntaxHighlighting text style -> Qt Creator category (highlighter.cpp).
STYLES = {
    "Normal": "Text",
    "Keyword": "Keyword",
    "Function": "Function",
    "Variable": "Local",
    "ControlFlow": "Keyword",
    "Operator": "Operator",
    "BuiltIn": "PrimitiveType",
    "Extension": "Global",
    "Preprocessor": "Preprocessor",
    "Attribute": "Attribute",
    "Char": "String",
    "SpecialChar": "String",
    "String": "String",
    "VerbatimString": "String",
    "SpecialString": "String",
    # Qt Creator's C++ editor draws an #include's file as a string
    # (highlighter.cpp maps Import to Macro, which shows no color).
    "Import": "String",
    "DataType": "Type",
    "DecVal": "Number",
    "BaseN": "Number",
    "Float": "Number",
    "Constant": "Enumeration",
    "Comment": "Comment",
    "Documentation": "Doxygen.Comment",
    "Annotation": "Doxygen.Tag",
    "CommentVar": "Doxygen.Tag",
    "RegionMarker": "Preprocessor",
    "Information": "Info",
    "Warning": "Warning",
    "Alert": "ErrorContext",
    "Error": "Error",
    "Others": "Text",
}

# The editor colors Kode does not take from a scheme (it draws with the
# system's), kept only so the themes are complete: Breeze's.
BREEZE = {
    "light": "breeze-light.theme",
    "dark": "breeze-dark.theme",
}


def read_scheme(path):
    """The scheme's styles, by name: {attribute: value}."""
    root = ElementTree.parse(path).getroot()
    return root.get("name"), {style.get("name"): dict(style.attrib) for style in root.iter("style")}


def complete(scheme):
    """The scheme with Qt Creator's fallbacks for what it leaves out."""
    styles = dict(scheme)
    for name, default in DEFAULTS.items():
        if name in styles:
            continue
        if name == "Macro" and "Function" in styles:
            styles[name] = styles["Function"]
        elif name == "Enumeration" and "Number" in scheme:
            styles[name] = styles["Number"]
        else:
            styles[name] = default
    return styles


def text_style(style):
    """A KSyntaxHighlighting text style from a Qt Creator style."""
    result = {}
    # Underline-only categories (diagnostics) show their underline color.
    color = style.get("foreground") or style.get("underlineColor")
    if color:
        result["text-color"] = color
        result["selected-text-color"] = color
    if style.get("bold") == "true":
        result["bold"] = True
    if style.get("italic") == "true":
        result["italic"] = True
    return result


def semantic_styles(scheme):
    """Each semantic category (and mixin) as a text style."""
    result = {}
    for name, default in SEMANTIC_DEFAULTS.items():
        fallback = SEMANTIC_FALLBACKS.get(name)
        if name in scheme:
            style = scheme[name]
        elif fallback in scheme:
            style = scheme[fallback]
        else:
            style = default
        result[name] = text_style(style)
    return result


def editor_colors(styles, kind):
    breeze = json.loads((ROOT / "external/syntax-highlighting/data/themes" / BREEZE[kind]).read_text())
    colors = dict(breeze["editor-colors"])
    mapped = {
        "BackgroundColor": ("Text", "background"),
        "TextSelection": ("Selection", "background"),
        "CurrentLine": ("CurrentLine", "background"),
        "LineNumbers": ("LineNumber", "foreground"),
        "CurrentLineNumber": ("CurrentLineNumber", "foreground"),
        "SearchHighlight": ("SearchResult", "background"),
        "BracketMatching": ("Parentheses", "background"),
    }
    for key, (name, attribute) in mapped.items():
        value = styles.get(name, {}).get(attribute)
        if value:
            colors[key] = value
    return colors


def make_theme(source, name, kind):
    scheme_name, scheme = read_scheme(source)
    styles = complete(scheme)
    return {
        "metadata": {
            "copyright": [
                "SPDX-FileCopyrightText: Rask Project",
                f"Colors from Qt Creator's \"{scheme_name}\" color scheme (tools/make-syntax-themes.py)",
            ],
            "license": "SPDX-License-Identifier: MIT",
            "revision": 1,
            "name": name,
        },
        "text-styles": {style: text_style(styles[category]) for style, category in STYLES.items()},
        "editor-colors": editor_colors(styles, kind),
        "semantic-styles": semantic_styles(scheme),
    }


def main():
    styles_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_STYLES
    out = ROOT / "assets/themes"
    out.mkdir(parents=True, exist_ok=True)
    for source, name, kind, file_name in (
        ("light-2024.xml", "Kode Light", "light", "kode-light.theme"),
        ("dark-2024.xml", "Kode Dark", "dark", "kode-dark.theme"),
    ):
        theme = make_theme(styles_dir / source, name, kind)
        (out / file_name).write_text(json.dumps(theme, indent=4) + "\n")
        print(out / file_name)


if __name__ == "__main__":
    main()
