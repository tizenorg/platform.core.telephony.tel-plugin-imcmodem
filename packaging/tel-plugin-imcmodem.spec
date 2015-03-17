%define major 0
%define minor 1
%define patchlevel 10

Name:             tel-plugin-imcmodem
Version:          %{major}.%{minor}.%{patchlevel}
Release:          1
License:          Apache-2.0
Summary:          telephony plugin library for AT communication with IMC modem
Group:            System/Libraries
Source0:          tel-plugin-imcmodem-%{version}.tar.gz
BuildRequires:    cmake
BuildRequires:    pkgconfig(glib-2.0)
BuildRequires:    pkgconfig(dlog)
BuildRequires:    pkgconfig(tcore)
Requires(post):   /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description
imcmodem plugin for telephony

%prep
%setup -q

%build
%cmake .
make %{?_smp_mflags}

%post
/sbin/ldconfig

%postun -p /sbin/ldconfig

%install
%make_install
mkdir -p %{buildroot}/usr/share/license
cp LICENSE %{buildroot}/usr/share/license/%{name}

%files
%manifest tel-plugin-imcmodem.manifest
%defattr(-,root,root,-)
%{_libdir}/telephony/plugins/*
/usr/share/license/%{name}
