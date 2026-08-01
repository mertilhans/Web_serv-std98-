#include "Client.hpp"

Client::Client()
	: listenSocket(NULL), localIp(0), remoteIp(0), lastActivity(0),
	readBuffer(""), writeBuffer(""),
	headersParsed(false), request(), contentLength(0), isChunked(false),
	keepAlive(false), matchedConfig(NULL), closeAfterWrite(true),
	chunkParseOffset(0), chunkedBody(""),
	requestStartTime(time(NULL))
{
}

Client::~Client()
{
}

Client::Client(const Client& ref)
	: listenSocket(ref.listenSocket), localIp(ref.localIp), remoteIp(ref.remoteIp),
	lastActivity(ref.lastActivity), readBuffer(ref.readBuffer), writeBuffer(ref.writeBuffer),
	headersParsed(ref.headersParsed), request(ref.request), contentLength(ref.contentLength),
	isChunked(ref.isChunked), keepAlive(ref.keepAlive), matchedConfig(ref.matchedConfig),
	closeAfterWrite(ref.closeAfterWrite), chunkParseOffset(ref.chunkParseOffset),
	chunkedBody(ref.chunkedBody), requestStartTime(ref.requestStartTime)
{
}

Client&	Client::operator=(const Client& ref)
{
	if (this != &ref)
	{
		listenSocket = ref.listenSocket;
		localIp = ref.localIp;
		remoteIp = ref.remoteIp;
		lastActivity = ref.lastActivity;
		readBuffer = ref.readBuffer;
		writeBuffer = ref.writeBuffer;
		headersParsed = ref.headersParsed;
		request = ref.request;
		contentLength = ref.contentLength;
		isChunked = ref.isChunked;
		keepAlive = ref.keepAlive;
		matchedConfig = ref.matchedConfig;
		closeAfterWrite = ref.closeAfterWrite;
		chunkParseOffset = ref.chunkParseOffset;
		chunkedBody = ref.chunkedBody;
		requestStartTime = ref.requestStartTime;
	}
	return (*this);
}

void	Client::resetForNextRequest()
{
	headersParsed = false;
	request = HttpRequest();
	contentLength = 0;
	isChunked = false;
	keepAlive = false;
	matchedConfig = NULL;
	chunkParseOffset = 0;
	chunkedBody.clear();
	// closeAfterWrite'a KASITLI OLARAK dokunmuyoruz: sendResponseAndCleanup
	// bu degeri (keep-alive kararina gore) BIZ cagrilmadan hemen once
	// set ediyor, ama writeClientData onu ANCAK yazma tamamlaninca (bu
	// reset'ten cok sonra, belki birkac poll() turu sonra) okuyacak --
	// burada true'ya sabitlersek az once verilen keep-alive karari kaybolur.
	// Yeni istek dongusu simdi basliyor sayilir -- Slowloris tarzi
	// "kucuk parcalarla idle timeout'u hep sifirlama" saldirisina karsi
	// bu zaman damgasi HER read()'de degil, SADECE burada yenilenir.
	requestStartTime = time(NULL);
}
