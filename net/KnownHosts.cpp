#include "KnownHosts.h"

#include "Sockets.h"      /* por red::CrearCarpeta, que es lo unico que cambia
                           * entre POSIX y Windows aca */

#include <cstdio>
#include <string>

namespace conocidos {

namespace {

const char kAlfabeto[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/** La carpeta de una ruta, o vacio si no tiene. Acepta las dos barras: en
 *  Windows la ruta puede venir con cualquiera de las dos. */
std::string CarpetaDe(const std::string &ruta)
{
	const std::size_t barra = ruta.find_last_of("/\\");
	if (barra == std::string::npos) return std::string();
	return ruta.substr(0, barra);
}

/** El ultimo byte del archivo, o 0 si esta vacio o no se pudo leer. */
char UltimoByte(const std::string &ruta)
{
	std::FILE *f = std::fopen(ruta.c_str(), "rb");
	if (f == nullptr) return 0;

	char c = 0;
	if (std::fseek(f, -1, SEEK_END) == 0)
	{
		if (std::fread(&c, 1, 1, f) != 1) c = 0;
	}
	std::fclose(f);
	return c;
}

} // namespace

std::string Base64(const unsigned char *datos, std::size_t largo)
{
	std::string r;
	if (datos == nullptr || largo == 0) return r;

	r.reserve(((largo + 2) / 3) * 4);

	std::size_t i = 0;
	while (i + 3 <= largo)
	{
		const unsigned int t = ((unsigned int)datos[i] << 16) |
		                       ((unsigned int)datos[i + 1] << 8) |
		                       (unsigned int)datos[i + 2];
		r += kAlfabeto[(t >> 18) & 0x3F];
		r += kAlfabeto[(t >> 12) & 0x3F];
		r += kAlfabeto[(t >> 6) & 0x3F];
		r += kAlfabeto[t & 0x3F];
		i += 3;
	}

	const std::size_t sobran = largo - i;
	if (sobran == 1)
	{
		const unsigned int t = (unsigned int)datos[i] << 16;
		r += kAlfabeto[(t >> 18) & 0x3F];
		r += kAlfabeto[(t >> 12) & 0x3F];
		r += "==";
	}
	else if (sobran == 2)
	{
		const unsigned int t = ((unsigned int)datos[i] << 16) |
		                       ((unsigned int)datos[i + 1] << 8);
		r += kAlfabeto[(t >> 18) & 0x3F];
		r += kAlfabeto[(t >> 12) & 0x3F];
		r += kAlfabeto[(t >> 6) & 0x3F];
		r += '=';
	}

	return r;
}

std::string NombreDeHost(const std::string &host, int puerto)
{
	if (puerto == 22 || puerto <= 0) return host;
	return "[" + host + "]:" + std::to_string(puerto);
}

std::string Linea(const std::string &host, int puerto, const std::string &tipo,
                  const unsigned char *clave, std::size_t largo)
{
	if (host.empty() || tipo.empty() || clave == nullptr || largo == 0)
		return std::string();

	return NombreDeHost(host, puerto) + " " + tipo + " " + Base64(clave, largo);
}

std::vector<Entrada> EntradasDe(const std::string &ruta, const std::string &host,
                                int *hasheadas)
{
	const Lectura l = Revisar(ruta, host, -1);
	if (hasheadas != nullptr) *hasheadas = l.hasheadas;
	return l.delHost;
}

Lectura Revisar(const std::string &ruta, const std::string &host, int puerto)
{
	Lectura r;

	if (ruta.empty() || host.empty()) return r;

	const std::string exacto = (puerto >= 0) ? NombreDeHost(host, puerto)
	                                         : std::string();

	std::FILE *f = std::fopen(ruta.c_str(), "rb");
	if (f == nullptr) return r;
	r.leido = true;

	std::string contenido;
	{
		char buf[4096];
		std::size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
			contenido.append(buf, n);
	}
	std::fclose(f);

	std::size_t i = 0;
	while (i <= contenido.size())
	{
		const std::size_t fin = contenido.find('\n', i);
		const std::string linea =
			contenido.substr(i, (fin == std::string::npos ? contenido.size() : fin) - i);
		i = (fin == std::string::npos) ? contenido.size() + 1 : fin + 1;

		/*  Se saltan los comentarios y las lineas vacias. Las marcas de
		 *  OpenSSH (@cert-authority, @revoked) corren los campos uno a la
		 *  derecha; no se interpretan, pero tampoco se confunden con un
		 *  nombre de host.                                                */
		std::size_t j = 0;
		while (j < linea.size() && (linea[j] == ' ' || linea[j] == '\t')) j++;
		if (j >= linea.size() || linea[j] == '#') continue;

		std::string campos[3];
		int cuantos = 0;
		while (j < linea.size() && cuantos < 3)
		{
			const std::size_t desde = j;
			while (j < linea.size() && linea[j] != ' ' && linea[j] != '\t') j++;
			campos[cuantos++] = linea.substr(desde, j - desde);
			while (j < linea.size() && (linea[j] == ' ' || linea[j] == '\t')) j++;
		}
		if (cuantos < 2) continue;

		int base = 0;
		if (!campos[0].empty() && campos[0][0] == '@') base = 1;
		if (cuantos < base + 2) continue;

		const std::string &nombres = campos[base];
		const std::string &tipo    = campos[base + 1];

		if (nombres.size() >= 3 && nombres.compare(0, 3, "|1|") == 0)
		{
			r.hasheadas++;
			continue;
		}

		/*  Un comodin puede alcanzar a este host sin nombrarlo, asi que
		 *  mientras haya alguno no se puede afirmar que el host no esta.  */
		if (nombres.find('*') != std::string::npos ||
		    nombres.find('?') != std::string::npos)
		{
			r.comodines++;
		}

		/*  Un mismo renglon puede nombrar varios hosts separados por coma. */
		std::size_t k = 0;
		while (k <= nombres.size())
		{
			const std::size_t coma = nombres.find(',', k);
			const std::string uno =
				nombres.substr(k, (coma == std::string::npos ? nombres.size() : coma) - k);
			k = (coma == std::string::npos) ? nombres.size() + 1 : coma + 1;

			/*  Coincide el nombre pelado, o cualquier "[host]:puerto": los
			 *  dos importan, porque confundirlos es una de las dos causas
			 *  posibles del mensaje de clave cambiada.                    */
			const bool pelado = (uno == host);
			const bool conPuerto =
				(uno.size() > host.size() + 2 && uno[0] == '[' &&
				 uno.compare(1, host.size(), host) == 0 &&
				 uno.compare(1 + host.size(), 2, "]:") == 0);

			if (pelado || conPuerto)
			{
				Entrada e;
				e.nombre = uno;
				e.tipo   = tipo;
				r.delHost.push_back(e);
				if (!exacto.empty() && uno == exacto) r.exacta = true;
			}
		}
	}

	return r;
}

bool Agregar(const std::string &ruta, const std::string &linea,
             std::string *error)
{
	std::string basura;
	if (error == nullptr) error = &basura;

	if (ruta.empty())
	{
		*error = "no se sabe donde esta known_hosts (no se pudo averiguar la "
		         "carpeta del usuario)";
		return false;
	}
	if (linea.empty())
	{
		*error = "no se pudo armar la linea para known_hosts";
		return false;
	}

	const std::string carpeta = CarpetaDe(ruta);
	if (!carpeta.empty() && !red::CrearCarpeta(carpeta))
	{
		*error = "no se pudo crear la carpeta " + carpeta;
		return false;
	}

	/*  Si el archivo venia sin salto al final, la entrada nueva se pegaria a
	 *  la ultima y se arruinarian las dos. Pasa mas seguido de lo que uno
	 *  cree con archivos editados a mano.                                 */
	const char ultimo = UltimoByte(ruta);
	const bool faltaSalto = (ultimo != 0 && ultimo != '\n');

	std::FILE *f = std::fopen(ruta.c_str(), "ab");
	if (f == nullptr)
	{
		*error = "no se pudo abrir " + ruta + " para agregar";
		return false;
	}

	bool bien = true;
	if (faltaSalto) bien = (std::fputc('\n', f) != EOF);
	if (bien)
		bien = (std::fwrite(linea.c_str(), 1, linea.size(), f) == linea.size());
	if (bien) bien = (std::fputc('\n', f) != EOF);

	if (std::fclose(f) != 0) bien = false;

	if (!bien)
	{
		*error = "no se pudo escribir en " + ruta;
		return false;
	}
	return true;
}

} // namespace conocidos
