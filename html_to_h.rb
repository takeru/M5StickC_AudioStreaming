s = File.read("index.html")
s.gsub!(/\n/, "\\n");
s.gsub!(/\"/, '\"');
#s = "#define _INDEX_HTML \"#{s}\""
s = "static const char* _INDEX_HTML = \"#{s}\";"
File.write("index_html.h", s)
