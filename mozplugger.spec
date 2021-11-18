%define name mozplugger
%define version 2.1.6
%define release 1

Summary: A generic mozilla plug-in
Name: %{name}
Version: %{version}
Release: %{release}
Source0: http://mozdev.mirrors.nyphp.org/mozplugger/%{name}-%{version}.tar.gz
Patch0: Makefile.patch
License: GPL
Group: Networking/WWW
BuildRoot: %{_tmppath}/%{name}-buildroot
Requires: m4
URL: http://mozplugger.mozdev.org/

%description
MozPlugger is a generic Mozilla plug-in that allows the use of standard Linux
programs as plug-ins for media types on the Internet.

%pre
%ifarch x86_64
if [ ! -d /usr/lib64/mozilla ]; then
ln -s /usr/lib64/mozilla-* /usr/lib/mozilla
fi
%else
if [ ! -d /usr/lib/mozilla ]; then
ln -s /usr/lib/mozilla-* /usr/lib/mozilla
fi
%endif

%prep
%setup -q
%patch0 -p1

%build
make linux

%install
make install root=$RPM_BUILD_ROOT

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root)
%config /etc/mozpluggerrc
%{_bindir}/*
%ifarch x86_64
/usr/lib64/mozilla/plugins/mozplugger.so
%else
/usr/lib/mozilla/plugins/mozplugger.so
%endif
%{_mandir}/man7/mozplugger.7*

%changelog
* Mon Feb 20 2006 William Bell <w.bell@physics.gla.ac.uk>
- Modified to allow 64bit version to be built.
* Mon Dec 20 2004 Louis Bavoil <bavoil@sci.utah.edu>
- removed dependency with Mozilla.
* Mon Feb  2 2004 MATSUURA Takanori <t-matsuu@estyle.ne.jp>
- rebuilt from spec file in mozplugger-1.5.0.tar.gz
- mozpluggerrc is packed as config file.
- added URL to spec file
# end of file
