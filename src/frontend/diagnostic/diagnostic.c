#include "luna/frontend/diagnostic/diagnostic.h"

#include <stdarg.h>

static void luna_diagnostic_print_source_line(FILE *stream,
                                              LunaSourceSpan span) {
    if (span.source == NULL || span.offset > span.source->length) {
        return;
    }

    size_t line_start = span.offset;
    while (line_start > 0U && span.source->text[line_start - 1U] != '\n') {
        line_start -= 1U;
    }

    size_t line_end = span.offset;
    while (line_end < span.source->length &&
           span.source->text[line_end] != '\n') {
        line_end += 1U;
    }

    (void)fputs("  ", stream);
    (void)fwrite(span.source->text + line_start, 1U, line_end - line_start,
                 stream);
    (void)fputc('\n', stream);
    (void)fputs("  ", stream);

    const size_t caret_column = span.offset - line_start;
    for (size_t index = 0U; index < caret_column; index += 1U) {
        const char character = span.source->text[line_start + index];
        (void)fputc(character == '\t' ? '\t' : ' ', stream);
    }

    const size_t available = line_end - span.offset;
    size_t caret_count = span.length == 0U ? 1U : span.length;
    if (available == 0U) {
        caret_count = 1U;
    } else if (caret_count > available) {
        caret_count = available;
    }
    for (size_t index = 0U; index < caret_count; index += 1U) {
        (void)fputc('^', stream);
    }
    (void)fputc('\n', stream);
}

void luna_diagnostic_init(LunaDiagnosticEngine *diagnostics, FILE *stream) {
    diagnostics->stream = stream == NULL ? stderr : stream;
    diagnostics->error_count = 0U;
}

void luna_diagnostic_error(LunaDiagnosticEngine *diagnostics,
                           LunaSourceSpan span, const char *format, ...) {
    diagnostics->error_count += 1U;

    const char *path = span.source == NULL || span.source->path == NULL
                           ? "<unknown>"
                           : span.source->path;
    (void)fprintf(diagnostics->stream, "%s:%u:%u: error: ", path, span.line,
                  span.column);

    va_list arguments;
    va_start(arguments, format);
    (void)vfprintf(diagnostics->stream, format, arguments);
    va_end(arguments);
    (void)fputc('\n', diagnostics->stream);

    luna_diagnostic_print_source_line(diagnostics->stream, span);
}

void luna_diagnostic_error_plain(LunaDiagnosticEngine *diagnostics,
                                 const char *format, ...) {
    diagnostics->error_count += 1U;
    (void)fputs("error: ", diagnostics->stream);

    va_list arguments;
    va_start(arguments, format);
    (void)vfprintf(diagnostics->stream, format, arguments);
    va_end(arguments);
    (void)fputc('\n', diagnostics->stream);
}

size_t luna_diagnostic_error_count(const LunaDiagnosticEngine *diagnostics) {
    return diagnostics->error_count;
}
