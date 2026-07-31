#ifndef LISTEN_TABLE_HPP
#define LISTEN_TABLE_HPP

#include <vector>
#include <map>
#include <string>
#include <ostream>
#include <stdint.h>
#include "ConfigStructs.hpp"

/**
 * @brief RealListen
 * Gercekten bind()+listen() edilecek TEK bir soket kaydi. Nginx mantigi:
 * bir portta wildcard (0.0.0.0) varsa o port icin SADECE BU kayitlardan
 * biri acilir; ayni portta duran spesifik IP'ler gercek socket almaz,
 * sadece routing metadata'sina donusur (bkz. ListenTable::resolve).
 */
struct RealListen
{
	uint32_t	ip;		// Network Byte Order
	uint16_t	port;	// Network Byte Order
	bool		isWildcard;
};

/**
 * @brief ListenTable
 * ConfigParser zaten field-level dogrulanmis ServerConfig listesini alir
 * ve "topology" seviyesindeki kontrolleri yapar: TEK bir ServerConfig'e
 * bakarak tespit edilemeyen, server'lari BIRBIRINE GORE kiyaslamayi
 * gerektiren kurallar. Somut olarak:
 *  - Ayni (ip,port) uzerinde iki server ayni server_name'e sahipse
 *    (Host header ile ayirt edilemez) reddedilir.
 *  - Ayni portta wildcard + spesifik IP varsa nginx gibi MERGE eder:
 *    gercekte TEK socket (wildcard) acilir, spesifik IP'ler runtime'da
 *    getsockname() sonucuna gore bu tablodan (resolve/chooseByHost) secilir.
 *
 * NOT: Ayni server'in kendi listen listesi icinde TAM duplicate (ip,port)
 * olup olmadigi burada TEKRAR kontrol edilmiyor -- ConfigParser bunu parse
 * aninda zaten garanti ediyor (_parseServerDirective, "listen" dali). Ayni
 * kontrolu iki yerde tekrar etmemek icin buradan kasitli olarak cikarildi.
 *
 * IP<->string donusumu ve case-insensitive karsilastirma artik bu sinifa
 * ozel degil -- NetUtils/StringUtils (paylasilan Utils katmani) kullanilir.
 */
class ListenTable
{
	private:
	ListenTable(const ListenTable& ref);
	ListenTable&	operator=(const ListenTable& ref);

	// build() ADIM 1: tum server'larin tum listen'larini port -> exact_ip ->
	// bucket seklinde _buckets icine toplar. NOT: IP<->string donusumu ve
	// case-insensitive karsilastirma artik NetUtils/StringUtils'te.
	void	_collectBuckets(std::vector<ServerConfig>& servers);
	// build() ADIM 2: her bucket icinde server_name capismasi var mi kontrol
	// eder; capisma varsa std::runtime_error firlatir.
	void	_checkServerNameConflicts() const;
	// build() ADIM 3: nginx-tarzi merge karariyla _realSockets'i doldurur
	// (wildcard varsa tek socket, yoksa her IP kendi socket'ini alir).
	void	_buildRealSockets();

	// port -> ( exact ip -> bucket )   (wildcard'in kendisi de bu haritada ip=0 ile durur)
	std::map<uint16_t, std::map<uint32_t, std::vector<ServerConfig*> > >	_buckets;
	std::vector<RealListen>	_realSockets;

public:
	ListenTable();
	~ListenTable();

	// Config'teki tum server bloklarindan bu tabloyu kurar.
	// Basarisiz olursa std::runtime_error firlatir (topology hatasi).
	void	build(std::vector<ServerConfig>& servers);

	const std::vector<RealListen>&	realSockets() const;

	// port + o portta gercekten baglanilan local ip verildiginde (accept()
	// sonrasi getsockname() ile, ikisi de Network Byte Order), hangi server
	// bucket'inin (server_name'e gore ayrisacak grup) kullanilacagini
	// dondurur. Eslesme yoksa NULL.
	const std::vector<ServerConfig*>*	resolve(uint16_t port, uint32_t localIp) const;

	// resolve() ile bulunan bucket ile, HTTP istegindeki Host header
	// degeri verildiginde NIHAI server'i secer.
	// - hostHeader bos ise veya hicbir aday ile (case-insensitive) tam
	//   eslesmiyorsa, bucket'taki ILK tanimlanan server "default server"
	//   olarak secilir (nginx'in davranisiyla ayni).
	// - bucket bos ise NULL doner.
	const ServerConfig*	chooseByHost(const std::vector<ServerConfig*>& bucket,
										const std::string& hostHeader) const;

	// Baslangicta her GERCEK socket icin hangi server_name(ler)'in o soket
	// uzerinden hizmet verdigini yazdirir. Bir wildcard socket, ayni portta
	// duran TUM spesifik-IP bucket'lari da (gercek socket almadiklari icin)
	// kendi uzerinden yurutur -- bu durumda o soketin altinda, hangi local
	// IP'ye hangi server_name dustugu ayri ayri satirlarda gosterilir (bir
	// gercek sokette birden fazla server_name olabilecegini acikca gostermek
	// icin -- bkz. nginx-tarzi wildcard/specific-IP merge).
	void	printListenSummary(std::ostream &os) const;
};

#endif
