// Test syntax macros (DSL)

// Define a simple DSL transformer (single line form)
syntax html => render_html(content)

// Define the render function
fn render_html content:
    println("Rendering HTML: ", content)
    return content

// Use the DSL
// Note: DSL blocks capture raw content
result = html:
    <div>Hello World</div>

println("Result: ", result)

// Another DSL example
syntax sql => db_query(content)

fn db_query query:
    println("Executing SQL: ", query)
    return 42

count = sql:
    SELECT COUNT(*) FROM users

println("Count: ", count)

println("Syntax macro test complete!")
