# #######################################################################
#
#   BSD LICENSE
#
#   Copyright(c) 2007-2023 Intel Corporation. All rights reserved.
#   All rights reserved.
#
#   Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions
#   are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#     * Neither the name of Intel Corporation nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# #######################################################################

SRCDIR  = src
TESTDIR = test

.PHONY: default
default: lib

.PHONY: lib
lib:
	$(Q)$(MAKE) -C $(SRCDIR) $@

.PHONY: test
test:
	$(Q)$(MAKE) -C $(TESTDIR) $@

.PHONY: benchmark
benchmark:
	$(Q)$(MAKE) -C $(TESTDIR) $@

.PHONY: install
install:
	$(Q)$(MAKE) -C $(SRCDIR) $@

.PHONY: uninstall
uninstall:
	$(Q)$(MAKE) -C $(SRCDIR) $@

clean:
	$(Q)$(MAKE) -C $(SRCDIR) $@
	$(Q)$(MAKE) -C $(TESTDIR) $@

########################
# RPM package building #
########################
rpm:
	mkdir -p rpmbuild/BUILD rpmbuild/RPMS rpmbuild/SOURCES rpmbuild/SPECS rpmbuild/SRPMS
	rpmbuild --undefine=_disable_source_fetch --define "_topdir $(PWD)/rpmbuild" -ba qat_zstd_plugin.spec

rpmclean:
	@rm -fr rpmbuild

.PHONY: rpm rpmclean
