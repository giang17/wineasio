#!/bin/bash
# Add TRACE to beginning of each asio_ function
sed -i '
/^static NTSTATUS asio_.*void \*args)$/,/^{$/ {
  /^{$/a\    TRACE("%s called\\n", __func__);
}
' asio_unix.c
