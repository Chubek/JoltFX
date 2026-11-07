# The .jtype files are the source of truth for both embedded and installed data.
set(jt_pagelets
    "Title|Basics|A configurable title frame.|design-elements"
    "Table|Basics|A fixed-em table with header and body.|design-elements"
    "DoubleRuleTable|Basics|Table Kliche with a double header rule.|design-elements"
    "Subtitle|Typography|A subtitle with an overline and accent.|subtitle"
    "SectionHeading|Typography|Numbered section heading with a rule.|section-heading"
    "Paragraph|Typography|A readable body paragraph with a lead-in.|paragraph"
    "PullQuote|Typography|An inset quotation with attribution.|pull-quote"
    "DropCap|Typography|A separate initial beside opening text.|drop-cap"
    "Byline|Typography|Author, role and publication date.|byline"
    "PageHeader|Navigation|Running publication header and section.|page-header"
    "PageFooter|Navigation|Footer with supplied page number and copyright.|page-footer"
    "ContentsList|Navigation|Three-entry contents list with page references.|contents-list"
    "Breadcrumbs|Navigation|Three-level section navigation.|breadcrumbs"
    "ChapterOpener|Navigation|Chapter number, title and introduction.|chapter-opener"
    "Callout|Editorial|Highlighted note with a colored side rule.|callout"
    "Sidebar|Editorial|A compact titled sidebar for related reading.|sidebar"
    "Caption|Editorial|Figure number, caption and credit.|caption"
    "FactBox|Editorial|Three labeled facts in a shaded box.|fact-box"
    "Footnote|Editorial|Reference marker and explanatory text.|footnote"
    "ContactCard|Business|Name, role and contact details.|contact-card"
    "AddressBlock|Business|Recipient and multi-line postal address.|address-block"
    "InvoiceHeader|Business|Invoice identity, issuer, client and dates.|invoice-header"
    "InvoiceTotals|Business|Supplied subtotal, tax and total values.|invoice-totals"
    "PriceCard|Business|Product, price and supporting details.|price-card"
    "SignatureBlock|Business|Signature and date lines with signer details.|signature-block"
    "KeyValueList|Data|Three rows of independent labels and values.|key-value-list"
    "ComparisonTable|Data|Three feature rows comparing two alternatives.|comparison-table"
    "Checklist|Data|Three printable checkboxes with editable labels.|checklist"
    "Timeline|Data|Three dated milestones along a vertical rule.|timeline"
    "Divider|Layout|Section separator with an editable center label.|divider"
    "TwoColumnText|Layout|Two independently editable text columns.|two-column-text"
    "ThreeColumnText|Layout|Three narrow text columns with headings.|three-column-text"
    "FigurePlaceholder|Layout|Framed figure placeholder and caption.|figure-placeholder")
set(JOLT_PAGELET_ENTRIES "")
foreach(record IN LISTS jt_pagelets)
    string(REPLACE "|" ";" fields "${record}")
    list(GET fields 0 name)
    list(GET fields 1 category)
    list(GET fields 2 description)
    list(GET fields 3 asset)
    set(source_file "${CMAKE_CURRENT_SOURCE_DIR}/pagelets/${asset}.jtype")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source_file}")
    file(READ "${source_file}" source)
    string(APPEND JOLT_PAGELET_ENTRIES "    {\"${name}\",\"${category}\",\"${description}\",\"${asset}\",R\"JT_PAGELET(${source})JT_PAGELET\"},\n")
endforeach()
configure_file(src/pagelets.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/pagelets.cpp" @ONLY)
target_sources(jolttype PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/pagelets.cpp")
