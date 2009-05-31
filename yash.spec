Name:		yash
Version:	2.10
Release:	1
Summary:	A POSIX-compliant command line shell

Group:		Applications/System
License:	GPLv2+
URL:		http://yash.sourceforge.jp/
Source:		http://yash.sourceforge.jp/releases/yash-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	bash, make, gcc >= 3.0, glibc-devel, ncurses-devel
Requires:	glibc, ncurses-libs

%description
Yash, yet another shell, is a command line shell that is intended to be the
most POSIX-compliant shell in the world.

%prep
%setup -q


%build
./configure --prefix=%{_prefix}
make %{?_smp_mflags}


%check
make test


%install
rm -rf $RPM_BUILD_ROOT
make DESTDIR=$RPM_BUILD_ROOT install


%clean
rm -rf $RPM_BUILD_ROOT


%post
grep -Fqx %{_bindir}/yash %{_sysconfdir}/shells || echo %{_bindir}/yash >>%{_sysconfdir}/shells


%preun
if [ $1 -eq 0 ]; then
grep -Fxv %{_bindir}/yash %{_sysconfdir}/shells >%{_sysconfdir}/shells.new
mv -f %{_sysconfdir}/shells.new %{_sysconfdir}/shells
fi


%files
%defattr(-,root,root,-)
%{_bindir}/yash
%doc README README.ja NEWS NEWS.ja



%changelog
* Sun May 31 2009 Watanabe Yuki <magicant@sourceforge.jp>
- version 2.10
