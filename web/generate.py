#!/usr/bin/env python3
"""
Generate web/v4d.html from the three Plan-V4D markdown documentation files.
Outputs a single self-contained HTML file with embedded CSS and JS.
"""

import re
import os
from pathlib import Path

# ─── Paths ────────────────────────────────────────────────────────────────────

REPO_ROOT = Path(__file__).resolve().parent.parent
DOCS = {
    "Readme": REPO_ROOT / "README.md",
    "V4D Application Programming Guide": REPO_ROOT / "modules/v4d/doc/v4d-application-programming-guide.markdown",
    "Plan-DSL Programming Guide": REPO_ROOT / "modules/plan/doc/plan-dsl-programming-guide.markdown",
    "Plan-DSL Reference (ISA)": REPO_ROOT / "modules/plan/doc/plan-dsl-reference.markdown",
}

OUTPUT = REPO_ROOT / "web" / "v4d.html"

# ─── Markdown → HTML converter ────────────────────────────────────────────────

def escape_html(text):
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

def md_inline(text):
    """Process inline markdown: bold, italic, code, links."""
    # Escape HTML first
    text = escape_html(text)
    # Bold
    text = re.sub(r'\*\*(.+?)\*\*', r'<strong>\1</strong>', text)
    # Italic
    text = re.sub(r'\*(.+?)\*', r'<em>\1</em>', text)
    # Inline code
    text = re.sub(r'`([^`]+)`', r'<code class="inline">\1</code>', text)
    # Links
    text = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<a href="\2">\1</a>', text)
    return text

def parse_table(lines):
    """Convert markdown table lines to HTML table."""
    if len(lines) < 2:
        return ""
    # Find separator line
    sep_idx = 1
    for i, line in enumerate(lines):
        if re.match(r'^\|[\s\-:|]+\|$', line.strip()):
            sep_idx = i
            break
    
    headers = []
    if sep_idx > 0:
        header_line = lines[0]
        headers = [cell.strip() for cell in header_line.strip().strip('|').split('|')]
    
    rows = []
    for line in lines[sep_idx + 1:]:
        cells = [cell.strip() for cell in line.strip().strip('|').split('|')]
        if cells:
            rows.append(cells)
    
    html = '<table class="api-table"><thead><tr>'
    for h in headers:
        html += f'<th>{md_inline(h)}</th>'
    html += '</tr></thead><tbody>'
    for row in rows:
        html += '<tr>'
        for cell in row[:len(headers)]:
            html += f'<td>{md_inline(cell)}</td>'
        html += '</tr>'
    html += '</tbody></table>'
    return html

def md_to_html(md_text, section_id_prefix=""):
    """Convert markdown text to HTML."""
    lines = md_text.split('\n')
    html_lines = []
    in_code_block = False
    code_lang = ""
    in_table = False
    table_lines = []
    in_list = False
    list_type = None
    heading_counter = {}
    section_id = 0
    
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        
        # Fenced code blocks
        if stripped.startswith('```'):
            if in_code_block:
                html_lines.append('</code></pre>')
                in_code_block = False
            else:
                code_lang = stripped[3:].strip()
                html_lines.append(f'<pre><code class="language-{code_lang}">')
                in_code_block = True
            i += 1
            continue
        
        if in_code_block:
            escaped = escape_html(line)
            html_lines.append(escaped)
            i += 1
            continue
        
        # Tables
        if '|' in stripped and stripped.startswith('|') and not in_code_block:
            if not in_table:
                in_table = True
                table_lines = []
            table_lines.append(stripped)
            i += 1
            continue
        elif in_table:
            html_lines.append(parse_table(table_lines))
            in_table = False
            table_lines = []
        
        # Empty line
        if not stripped:
            if in_list:
                html_lines.append(f'</{list_type}>')
                in_list = False
                list_type = None
            i += 1
            continue
        
        # Headings
        if stripped.startswith('#'):
            level = 0
            while level < len(stripped) and stripped[level] == '#':
                level += 1
            text = stripped[level:].strip()
            # Generate ID
            base_id = re.sub(r'[^\w]+', '-', text.lower()).strip('-')
            heading_counter[base_id] = heading_counter.get(base_id, 0) + 1
            anchor = f"{base_id}-{heading_counter[base_id]}" if heading_counter[base_id] > 1 else base_id
            html_lines.append(f'<h{level} id="{anchor}">{md_inline(text)}</h{level}>')
            i += 1
            continue
        
        # Horizontal rule
        if stripped == '---':
            html_lines.append('<hr>')
            i += 1
            continue
        
        # Blockquote
        if stripped.startswith('> '):
            quote_text = stripped[2:].strip()
            html_lines.append(f'<blockquote>{md_inline(quote_text)}</blockquote>')
            i += 1
            continue
        
        # Unordered list
        ul_match = re.match(r'^(\s*)([-*+])\s+(.*)', stripped)
        if ul_match:
            indent = len(ul_match.group(1))
            text = ul_match.group(3)
            if not in_list or list_type != 'ul':
                if in_list:
                    html_lines.append(f'</{list_type}>')
                html_lines.append('<ul>')
                in_list = True
                list_type = 'ul'
            html_lines.append(f'<li>{md_inline(text)}</li>')
            i += 1
            continue
        
        # Ordered list
        ol_match = re.match(r'^(\s*)(\d+\.)\s+(.*)', stripped)
        if ol_match:
            indent = len(ol_match.group(1))
            text = ol_match.group(3)
            if not in_list or list_type != 'ol':
                if in_list:
                    html_lines.append(f'</{list_type}>')
                html_lines.append('<ol>')
                in_list = True
                list_type = 'ol'
            html_lines.append(f'<li>{md_inline(text)}</li>')
            i += 1
            continue
        
        # Paragraph
        html_lines.append(f'<p>{md_inline(stripped)}</p>')
        i += 1
    
    # Close any open elements
    if in_table:
        html_lines.append(parse_table(table_lines))
    if in_list:
        html_lines.append(f'</{list_type}>')
    
    return '\n'.join(html_lines)

# ─── Navigation builder ───────────────────────────────────────────────────────

def build_navigation(sections):
    """Build nested navigation from parsed sections."""
    nav_html = '<ul class="nav-list">'
    for sec in sections:
        nav_html += f'<li class="nav-item"><a href="#{sec["id"]}" class="nav-link">{escape_html(sec["title"])}</a>'
        if sec["children"]:
            nav_html += '<ul class="nav-list">'
            for child in sec["children"]:
                nav_html += f'<li class="nav-item"><a href="#{child["id"]}" class="nav-link">{escape_html(child["title"])}</a></li>'
            nav_html += '</ul>'
        nav_html += '</li>'
    nav_html += '</ul>'
    return nav_html

# ─── Section parser ───────────────────────────────────────────────────────────

def parse_sections(md_text, prefix=""):
    """Split markdown into top-level sections based on ## headings."""
    sections = []
    current = None
    lines = md_text.split('\n')
    
    for line in lines:
        if line.startswith('## '):
            if current and not re.match(r'^table\s+of\s+contents$', current["title"], re.IGNORECASE):
                sections.append(current)
            title = line[3:].strip()
            if re.match(r'^table\s+of\s+contents$', title, re.IGNORECASE):
                current = None
                continue
            base_id = re.sub(r'[^\w]+', '-', title.lower()).strip('-')
            current = {
                "title": title,
                "id": base_id,
                "content": "",
                "children": [],
                "prefix": prefix,
            }
        elif line.startswith('### ') and current:
            child_title = line[4:].strip()
            child_id = re.sub(r'[^\w]+', '-', child_title.lower()).strip('-')
            current["children"].append({
                "title": child_title,
                "id": child_id,
                "content": "",
            })
        elif current is not None:
            if current["children"]:
                current["children"][-1]["content"] += line + "\n"
            else:
                current["content"] += line + "\n"
    
    if current and not re.match(r'^table\s+of\s+contents$', current["title"], re.IGNORECASE):
        sections.append(current)
    
    return sections

# ─── HTML Template ────────────────────────────────────────────────────────────

CSS = """\
:root {
    --bg-primary: #ffffff;
    --bg-secondary: #f8f9fa;
    --bg-sidebar: #f1f3f5;
    --text-primary: #212529;
    --text-secondary: #495057;
    --text-muted: #6c757d;
    --accent: #0056b3;
    --accent-hover: #004494;
    --border: #dee2e6;
    --code-bg: #f4f4f4;
    --code-dark-bg: #1e1e1e;
    --code-dark-text: #d4d4d4;
    --shadow: 0 2px 8px rgba(0,0,0,0.08);
    --sidebar-width: 280px;
    --header-height: 56px;
}

* { box-sizing: border-box; margin: 0; padding: 0; }

html { scroll-behavior: smooth; }

body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
    color: var(--text-primary);
    background: var(--bg-primary);
    line-height: 1.7;
    display: flex;
    flex-direction: column;
    min-height: 100vh;
}

/* Header */
.header {
    position: fixed;
    top: 0; left: 0; right: 0;
    height: var(--header-height);
    background: var(--bg-primary);
    border-bottom: 1px solid var(--border);
    display: flex;
    align-items: center;
    padding: 0 24px;
    z-index: 1000;
    box-shadow: var(--shadow);
}

.header-title {
    font-size: 1.1rem;
    font-weight: 600;
    color: var(--accent);
    margin-right: 32px;
    white-space: nowrap;
}

.header-title a {
    color: inherit;
    text-decoration: none;
}

/* Layout */
.layout {
    display: flex;
    margin-top: var(--header-height);
    min-height: calc(100vh - var(--header-height));
}

/* Sidebar */
.sidebar {
    width: var(--sidebar-width);
    position: fixed;
    top: var(--header-height);
    bottom: 0;
    left: 0;
    overflow-y: auto;
    background: var(--bg-sidebar);
    border-right: 1px solid var(--border);
    padding: 16px 0;
    z-index: 900;
}

.sidebar-section {
    margin-bottom: 8px;
}

.sidebar-title {
    padding: 8px 20px;
    font-size: 0.75rem;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    color: var(--text-muted);
    cursor: pointer;
    user-select: none;
    display: flex;
    justify-content: space-between;
    align-items: center;
}

.sidebar-title::after {
    content: "▼";
    font-size: 0.6rem;
    transition: transform 0.2s;
}

.sidebar-section.collapsed .sidebar-title::after {
    transform: rotate(-90deg);
}

.sidebar-list {
    list-style: none;
    padding: 0;
    overflow: hidden;
    transition: max-height 0.3s ease;
}

.sidebar-section.collapsed .sidebar-list {
    max-height: 0 !important;
}

.nav-item {
    margin: 0;
}

.nav-link {
    display: block;
    padding: 6px 20px 6px 28px;
    color: var(--text-secondary);
    text-decoration: none;
    font-size: 0.88rem;
    border-left: 3px solid transparent;
    transition: all 0.15s;
}

.nav-link:hover,
.nav-link.active {
    color: var(--accent);
    background: rgba(0,86,179,0.05);
    border-left-color: var(--accent);
}

.nav-link.active {
    font-weight: 600;
}

/* Main content */
.main {
    flex: 1;
    margin-left: var(--sidebar-width);
    padding: 32px 48px 64px;
    max-width: calc(100% - var(--sidebar-width));
}

.doc-section {
    max-width: 900px;
    margin: 0 auto;
    padding-bottom: 48px;
    border-bottom: 1px solid var(--border);
    margin-bottom: 48px;
}

.doc-section:last-child {
    border-bottom: none;
}

.doc-section h1 {
    font-size: 2rem;
    margin-bottom: 8px;
    color: var(--text-primary);
    border-bottom: 2px solid var(--accent);
    padding-bottom: 8px;
}

.doc-section h2 {
    font-size: 1.5rem;
    margin-top: 40px;
    margin-bottom: 16px;
    color: var(--text-primary);
    scroll-margin-top: calc(var(--header-height) + 16px);
}

.doc-section h3 {
    font-size: 1.2rem;
    margin-top: 28px;
    margin-bottom: 12px;
    color: var(--text-primary);
    scroll-margin-top: calc(var(--header-height) + 16px);
}

.doc-section h4 {
    font-size: 1rem;
    margin-top: 20px;
    margin-bottom: 8px;
    color: var(--text-secondary);
}

.doc-section p {
    margin-bottom: 14px;
    color: var(--text-secondary);
}

.doc-section ul, .doc-section ol {
    margin-bottom: 14px;
    padding-left: 24px;
    color: var(--text-secondary);
}

.doc-section li {
    margin-bottom: 6px;
}

.doc-section hr {
    border: none;
    border-top: 1px solid var(--border);
    margin: 32px 0;
}

.doc-section blockquote {
    border-left: 4px solid var(--accent);
    padding: 12px 20px;
    margin: 16px 0;
    background: var(--bg-secondary);
    color: var(--text-secondary);
    border-radius: 0 6px 6px 0;
}

.doc-section strong {
    color: var(--text-primary);
    font-weight: 600;
}

.doc-section em {
    font-style: italic;
}

.doc-section a {
    color: var(--accent);
    text-decoration: none;
}

.doc-section a:hover {
    text-decoration: underline;
}

/* Code blocks */
pre {
    background: var(--code-dark-bg);
    color: var(--code-dark-text);
    padding: 16px 20px;
    border-radius: 8px;
    overflow-x: auto;
    margin: 16px 0;
    font-family: 'SF Mono', 'Fira Code', 'Fira Mono', Menlo, Consolas, monospace;
    font-size: 0.85rem;
    line-height: 1.6;
    box-shadow: var(--shadow);
}

code.inline {
    background: var(--code-bg);
    padding: 2px 6px;
    border-radius: 4px;
    font-family: 'SF Mono', 'Fira Code', 'Fira Mono', Menlo, Consolas, monospace;
    font-size: 0.85em;
    color: #d6336c;
}

pre code {
    background: none;
    padding: 0;
    color: inherit;
    font-size: inherit;
}

/* Tables */
.api-table {
    width: 100%;
    border-collapse: collapse;
    margin: 16px 0;
    font-size: 0.9rem;
    box-shadow: var(--shadow);
    border-radius: 8px;
    overflow: hidden;
}

.api-table th {
    background: var(--bg-sidebar);
    font-weight: 600;
    text-align: left;
    padding: 10px 14px;
    border-bottom: 2px solid var(--border);
    color: var(--text-primary);
}

.api-table td {
    padding: 10px 14px;
    border-bottom: 1px solid var(--border);
    color: var(--text-secondary);
}

.api-table tr:last-child td {
    border-bottom: none;
}

.api-table tr:hover td {
    background: var(--bg-secondary);
}

/* Back to top */
.back-to-top {
    position: fixed;
    bottom: 24px;
    right: 24px;
    width: 44px;
    height: 44px;
    background: var(--accent);
    color: white;
    border: none;
    border-radius: 50%;
    cursor: pointer;
    font-size: 1.2rem;
    display: none;
    align-items: center;
    justify-content: center;
    box-shadow: 0 4px 12px rgba(0,0,0,0.2);
    z-index: 800;
    transition: opacity 0.2s;
}

.back-to-top.visible {
    display: flex;
}

.back-to-top:hover {
    background: var(--accent-hover);
}

/* Mobile */
@media (max-width: 768px) {
    .sidebar {
        transform: translateX(-100%);
        transition: transform 0.3s;
    }
    .sidebar.open {
        transform: translateX(0);
    }
    .main {
        margin-left: 0;
        max-width: 100%;
        padding: 24px 20px 48px;
    }
    .menu-toggle {
        display: block !important;
    }
}

.menu-toggle {
    display: none;
    background: none;
    border: none;
    font-size: 1.5rem;
    cursor: pointer;
    margin-right: 12px;
    color: var(--text-primary);
}

/* Scrollbar */
.sidebar::-webkit-scrollbar {
    width: 6px;
}

.sidebar::-webkit-scrollbar-thumb {
    background: var(--border);
    border-radius: 3px;
}

.sidebar::-webkit-scrollbar-thumb:hover {
    background: var(--text-muted);
}

/* Table of contents within sections */
.toc {
    background: var(--bg-secondary);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 16px 20px;
    margin-bottom: 24px;
}

.toc-title {
    font-weight: 600;
    margin-bottom: 8px;
    color: var(--text-primary);
}

.toc ul {
    list-style: none;
    padding-left: 0;
    margin: 0;
}

.toc li {
    margin: 4px 0;
}

.toc a {
    color: var(--text-secondary);
    text-decoration: none;
    font-size: 0.9rem;
}

.toc a:hover {
    color: var(--accent);
}

/* Admonitions / callouts */
.callout {
    padding: 14px 18px;
    border-radius: 8px;
    margin: 16px 0;
    border-left: 4px solid;
}

.callout.info {
    background: #e7f3ff;
    border-left-color: var(--accent);
    color: #084298;
}

.callout.warning {
    background: #fff3cd;
    border-left-color: #f0ad4e;
    color: #664d03;
}

.callout.danger {
    background: #f8d7da;
    border-left-color: #dc3545;
    color: #842029;
}

/* Section anchors */
.section-anchor {
    color: var(--text-muted);
    text-decoration: none;
    margin-left: 4px;
    font-size: 0.8em;
    opacity: 0;
    transition: opacity 0.2s;
}

h1:hover .section-anchor,
h2:hover .section-anchor,
h3:hover .section-anchor {
    opacity: 1;
}
"""

JS = """\
(function() {
    'use strict';

    // ── Sidebar collapse ──────────────────────────────────────────────────────
    document.querySelectorAll('.sidebar-title').forEach(title => {
        title.addEventListener('click', () => {
            title.parentElement.classList.toggle('collapsed');
        });
    });

    // ── Active nav highlighting ───────────────────────────────────────────────
    const mainContent = document.querySelector('.main');
    const navLinks = document.querySelectorAll('.nav-link');
    const headings = document.querySelectorAll('.doc-section h1, .doc-section h2, .doc-section h3');

    function updateActiveNav() {
        let currentId = '';
        const scrollPos = window.scrollY + 100;
        headings.forEach(h => {
            if (h.offsetTop <= scrollPos) {
                currentId = h.id;
            }
        });
        navLinks.forEach(link => {
            link.classList.toggle('active', link.getAttribute('href') === '#' + currentId);
        });
    }

    window.addEventListener('scroll', updateActiveNav, { passive: true });
    updateActiveNav();

    // ── Helpers ─────────────────────────────────────────────────────────────
    function escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    }

    function escapeRegex(string) {
        return string.replace(/[.*+?^${}()|[\\]\\\\]/g, '\\\\$&');
    }

    // ── Sidebar collapse ──────────────────────────────────────────────────────
    document.querySelectorAll('.sidebar-title').forEach(title => {
        title.addEventListener('click', () => {
            title.parentElement.classList.toggle('collapsed');
        });
    });

    // ── Active nav highlighting ───────────────────────────────────────────────
    const mainContent = document.querySelector('.main');
    const navLinks = document.querySelectorAll('.nav-link');
    const headings = document.querySelectorAll('.doc-section h1, .doc-section h2, .doc-section h3');

    function updateActiveNav() {
        let currentId = '';
        const scrollPos = window.scrollY + 100;
        headings.forEach(h => {
            if (h.offsetTop <= scrollPos) {
                currentId = h.id;
            }
        });
        navLinks.forEach(link => {
            link.classList.toggle('active', link.getAttribute('href') === '#' + currentId);
        });
    }

    window.addEventListener('scroll', updateActiveNav, { passive: true });
    updateActiveNav();

    // ── Back to top ────────────────────────────────────────────────────────────
    const backToTop = document.getElementById('back-to-top');
    window.addEventListener('scroll', () => {
        backToTop.classList.toggle('visible', window.scrollY > 500);
    }, { passive: true });
    backToTop.addEventListener('click', () => {
        window.scrollTo({ top: 0, behavior: 'smooth' });
    });

    // ── Mobile menu ────────────────────────────────────────────────────────────
    const menuToggle = document.getElementById('menu-toggle');
    const sidebar = document.querySelector('.sidebar');
    menuToggle.addEventListener('click', () => {
        sidebar.classList.toggle('open');
    });

    // Close sidebar on mobile when clicking a link
    document.querySelectorAll('.nav-link').forEach(link => {
        link.addEventListener('click', () => {
            if (window.innerWidth <= 768) {
                sidebar.classList.remove('open');
            }
        });
    });

    // ── Smooth scroll offset for fixed header ──────────────────────────────────
    document.querySelectorAll('a[href^="#"]').forEach(anchor => {
        anchor.addEventListener('click', function(e) {
            const targetId = this.getAttribute('href').substring(1);
            const target = document.getElementById(targetId);
            if (target) {
                e.preventDefault();
                const offset = 80;
                const top = target.getBoundingClientRect().top + window.scrollY - offset;
                window.scrollTo({ top, behavior: 'smooth' });
            }
        });
    });

    // ── Copy code blocks ───────────────────────────────────────────────────────
    document.querySelectorAll('pre').forEach(pre => {
        pre.addEventListener('click', () => {
            const code = pre.querySelector('code');
            if (code) {
                navigator.clipboard.writeText(code.textContent).then(() => {
                    const btn = document.createElement('span');
                    btn.textContent = ' (copied!)';
                    btn.style.color = '#4ade80';
                    btn.style.fontSize = '0.8rem';
                    pre.appendChild(btn);
                    setTimeout(() => btn.remove(), 1500);
                });
            }
        });
    });
})();
"""

# ─── Content extraction ───────────────────────────────────────────────────────

def extract_content(section):
    """Extract HTML content from a parsed section."""
    parts = []
    
    # Section heading (already in the parent, skip for children)
    if section.get("content"):
        parts.append(md_to_html(section["content"]))
    
    for child in section.get("children", []):
        if child.get("content"):
            parts.append(md_to_html(child["content"]))
    
    return "\n".join(parts)

# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    print("Reading source markdown files...")
    all_sections = []
    nav_items = []
    
    for doc_title, doc_path in DOCS.items():
        if not doc_path.exists():
            print(f"WARNING: {doc_path} not found, skipping.")
            continue
        
        with open(doc_path, 'r', encoding='utf-8') as f:
            md_text = f.read()
        
        print(f"Processing: {doc_title}")
        sections = parse_sections(md_text, prefix=doc_title)
        
        for sec in sections:
            sec_id = sec["id"]
            # Ensure uniqueness across documents
            full_id = f"{re.sub(r'[^a-z0-9]+', '-', doc_title.lower()).strip('-')}-{sec_id}"
            sec["id"] = full_id
            
            # Build nav entry first
            nav_entry = {
                "title": sec["title"],
                "id": full_id,
                "doc": doc_title,
                "children": []
            }
            
            # Build section HTML: parent content + children with headings
            sec_html_parts = []
            if sec.get("content"):
                sec_html_parts.append(md_to_html(sec["content"]))
            
            for child in sec.get("children", []):
                child_id = f"{full_id}-{child['id']}"
                child["id"] = child_id
                child_content = child.get("content", "")
                sec_html_parts.append(f'<h2 id="{child_id}">{escape_html(child["title"])}</h2>')
                sec_html_parts.append(md_to_html(child_content))
                
                nav_entry["children"].append({
                    "title": child["title"],
                    "id": child_id,
                })
            
            sec["html"] = "\n".join(sec_html_parts)
            nav_items.append(nav_entry)
            all_sections.append(sec)
    
    print(f"Total sections: {len(all_sections)}")
    
    # Build sidebar HTML
    sidebar_html = ""
    current_doc = None
    for item in nav_items:
        if item["doc"] != current_doc:
            if current_doc is not None:
                sidebar_html += "</ul>"
            current_doc = item["doc"]
            sidebar_html += f'<div class="sidebar-section"><div class="sidebar-title">{escape_html(current_doc)}</div><ul class="sidebar-list">'
        
        sidebar_html += f'<li class="nav-item"><a href="#{item["id"]}" class="nav-link">{escape_html(item["title"])}</a>'
        if item["children"]:
            sidebar_html += "<ul class=\"nav-list\">"
            for child in item["children"]:
                sidebar_html += f'<li class="nav-item"><a href="#{child["id"]}" class="nav-link">{escape_html(child["title"])}</a></li>'
            sidebar_html += "</ul>"
        sidebar_html += "</li>"
    
    if current_doc is not None:
        sidebar_html += "</ul></div>"
    
    # Build main content HTML
    content_html = ""
    for sec in all_sections:
        content_html += f'<div class="doc-section" id="{sec["id"]}" data-doc="{escape_html(sec.get("prefix", ""))}">\n'
        content_html += f'<h2>{escape_html(sec["title"])}<a href="#{sec["id"]}" class="section-anchor">#</a></h2>\n'
        content_html += sec.get("html", "") + '\n'
        content_html += "</div>\n"
    
    # Assemble final HTML
    final_html = f"""\
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Plan-V4D Documentation</title>
    <style>
        {CSS}
    </style>
</head>
<body>
    <header class="header">
         <button class="menu-toggle" id="menu-toggle" aria-label="Toggle menu">☰</button>
         <div class="header-title">
             <a href="https://viel-zu.org/opencv/doxygen/html/d7/dfc/v4d.html">Plan-V4D</a>
         </div>
     </header>
     <div class="layout">
         <aside class="sidebar" id="sidebar">
             {sidebar_html}
         </aside>
        <main class="main">
            {content_html}
        </main>
    </div>
    <button class="back-to-top" id="back-to-top" aria-label="Back to top">↑</button>
    <script>
        {JS}
    </script>
</body>
</html>
"""
    
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUTPUT, 'w', encoding='utf-8') as f:
        f.write(final_html)
    
    print(f"Generated: {OUTPUT}")
    print(f"Size: {len(final_html):,} bytes")

if __name__ == "__main__":
    main()
