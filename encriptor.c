#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>

#define MAX_WORDS 1024
#define MAX_WORD_LEN 1024
#define MAX_BUFFER 1024

struct InfoWords {
	int start_pos;	// de unde incepe un cuvant in words
	int len;		// ce lungime are cuvantul
};

struct InfoPerms {
	int start_pos;	// de unde incepe o permutare in perms
	int len;		// ce lungime are permutarea
};


int main(int argc, char *argv[]) {
	// deschid fisierul cu cuvinte
	int words_fd = open(argv[1], O_RDWR);
		
	// iau dimensiunea fisierului
	struct stat sb;
	if (fstat(words_fd, &sb) == -1) {
		perror("file size\n");
		return errno;
	}
		
	// mapez fisierul in memorie
	char *words = mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, words_fd, 0);
	
	// pentru fiecare cuvant trebuie sa stiu de unde incepe in words si cat de lung e
	struct InfoWords info[MAX_WORDS];
	
	// parcurg words si completez informatiile (start_pos si len)
	int num_words = 0;
	int l = 0;
	int pos = 0;
	for (int i = 0; i < sb.st_size; i ++) {
		if (words[i] != ' ' && words[i] != '\n') {
			l ++;
		}
		else {
			info[num_words].start_pos = pos;
			info[num_words].len = l;
			l = 0;
			num_words ++;
			pos = i + 1;
		}
	}
	
	
	// creez shm unde sa scrie procesele permutarile / cuvintele decriptate
	char shm_name[] = "myshm";
	int shm_fd;
	shm_fd = shm_open(shm_name, O_CREAT | O_RDWR , S_IRUSR | S_IWUSR);
	
	if (shm_fd < 0) {
		perror("shmf_fd\n");
		return errno;
	}
		
	int dim = getpagesize();	// cati bytes are fiecare proces sa scrie
	size_t shm_size = num_words * dim;
	
	if (ftruncate(shm_fd, shm_size) == -1) {
		perror("ftruncate\n");
		shm_unlink(shm_name);
		return errno ;
	}

	// criptare
	// primesc fisierul cu cuvintele decriptate
	// le criptez si modific fisierul sa fie cuvintele criptate
	// + creez un fisier in care scriu permutarile
	
	if (argc == 2) {
		// fiecare proces primeste un cuvant pe care il cripteaza si apoi il copiaza in words
		// peste cuvantul initial
		// parcurg words si pornesc cate un proces pentru ficare cuvant
		for (int i = 0; i < num_words; i ++) {
			pid_t pid = fork();
			if (pid < 0) {
				perror("error creating process\n");
				return errno;
			}
			else if (pid == 0) {
				// child
				// generez o permutare
				int n = info[i].len;
				int cn = n;
				int perm[n];
				for (int j = 0; j < n; j ++) {
					perm[j] = j;
				}
				
				while (n) {
					srand(time(NULL));
					// iau un index in vector
					int index = rand() % n;
					int x = perm[index];
					// "sterg" numarul din vector
					// il schimb cu cel de pe ultima pozitie si scad n-ul
					int aux = perm[n-1];
					perm[n-1] = perm[index];
					perm[index] = aux;
					n --;
				}
				
				
				// transform permutarea in string
				n = cn;

				// calculez lungimea lui perm_string
				int total_len = 0;
				for (int i = 0; i < n; i ++) {
					total_len += snprintf(NULL, 0, "%d ", perm[i]);
				}

				char perm_string[total_len];

				// copiez permutarea in perm_string
				int pos = 0;
				for (int i = 0; i < n; i++) {
					pos += snprintf(perm_string + pos, sizeof(perm_string) - pos, "%d", perm[i]);
					// pentru spatiu
					if (i < n - 1) {
						perm_string[pos] = ' ';
						pos ++;
					}
				}
				perm_string[pos] = '\0';
				
				// scriu permutarea in shm
				void *shm_ptr = mmap(0, dim, PROT_WRITE, MAP_SHARED, shm_fd, i * dim);
			
				if (shm_ptr == MAP_FAILED) {
					perror("shm_ptr child\n");
					shm_unlink(shm_name);
					return errno ;
				}
				
				memcpy(shm_ptr, perm_string, sizeof(perm_string));
				munmap(shm_ptr, dim);
				
				
				// criptez cuvantul
				char cripted_word[info[i].len];
				for (int j = 0; j < info[i].len; j ++) {
					cripted_word[j] = words[info[i].start_pos + perm[j]];
				}
				cripted_word[info[i].len] = '\0';
				
				// copiez cuvantul criptat in words
				for (int j = 0; j < info[i].len; j ++) {
					words[info[i].start_pos + j] = cripted_word[j];
				}
				exit(0);
			}
		}
		
		// astept sa se termine toate procesele
		for (int i = 0; i < num_words; i ++) {
			wait(NULL);
		}
		
		// creez fisierul permutations.txt in care o sa scriu permutarile
		FILE *perm_ptr = fopen("permutations.txt", "w");
		
		// incarc shm in care am permutarile
		void *shm_ptr = mmap(0, num_words * dim, PROT_READ, MAP_SHARED, shm_fd, 0);
			
		if (shm_ptr == MAP_FAILED) {
			perror("shm_ptr parent\n");
			shm_unlink(shm_name);
			return errno ;
		}
		
		// scriu permutarile in permutations.txt
		char buffer[MAX_BUFFER];
		for (int i = 0; i < num_words; i ++) {
			int p = 0;
			strncpy(buffer, shm_ptr, MAX_BUFFER - 1);
			shm_ptr += dim;
			fprintf(perm_ptr, "%s\n", buffer);
		}
		
		shm_ptr -= (num_words) * dim;
		munmap(shm_ptr, shm_size);
		shm_unlink(shm_name);
		fclose(perm_ptr);
	}
	
	
	// decriptare
	// primesc fisierul cu cuvintele criptate si fisierul cu permutarile
	// decriptez cuvintele si creez un fisier in care scriu cuvintele decriptate
	
	if (argc == 3) {
		// deschid fisierul cu permutari
		int perms_fd = open(argv[2], O_RDONLY);	// de schimbat in read only
			
		// iau dimensiunea fisierului
		struct stat sb2;
		if (fstat(perms_fd, &sb2) == -1) {
			perror("file size\n");
			return errno;
		}
			
		// mapez fisierul in memorie
		char *perms = mmap(NULL, sb2.st_size, PROT_READ, MAP_SHARED, perms_fd, 0);
		
		// pentru fiecare permutare trebuie sa stiu de unde incepe in perms si cat de lunga e
		struct InfoPerms info2[MAX_WORDS];
		
		// parcurg perms si completez informatiile (start_pos si len)
		int num_perms = 0;
		int l = 0;
		int pos = 0;
		for (int i = 0; i < sb2.st_size; i ++) {
			if (perms[i] != '\n') {
				l ++;
			}
			else {
				info2[num_perms].start_pos = pos;
				info2[num_perms].len = l;
				l = 0;
				num_perms ++;
				pos = i + 1;
			}
		}
	
		// fiecare proces primeste un cuvant pe care il decripteaza si apoi il scrie in shm
		// parcurg words si pornesc cate un proces pentru ficare cuvant
		for (int i = 0; i < num_words; i ++) {
			pid_t pid = fork();
			if (pid < 0) {
				perror("error creating process\n");
				return errno;
			}
			else if (pid == 0) {
				// child
				// iau permutarea din perms si o pun intr-un vector de int
				int perm[info[i].len];
				int pos = 0, num = 0;
				for (int j = 0; j < info2[i].len; j ++) {
					if (perms[info2[i].start_pos + j] == ' ') {
						perm[pos] = num;
						pos ++;
						num = 0;
					}
					else {
						num = num * 10 + (perms[info2[i].start_pos + j] - '0');
					}
				}
				perm[pos] = num;
				
				// decriptez cuvantul
				char decripted_word[info[i].len];
				for (int j = 0; j < info[i].len; j ++) {
					decripted_word[perm[j]] = words[info[i].start_pos + j];
				}
				decripted_word[info[i].len] = '\0';
				
				// scriu cuvantul decriptat in shm
				void *shm_ptr = mmap(0, dim, PROT_WRITE, MAP_SHARED, shm_fd, i * dim);
			
				if (shm_ptr == MAP_FAILED) {
					perror("shm_ptr child\n");
					shm_unlink(shm_name);
					return errno ;
				}
				
				memcpy(shm_ptr, decripted_word, sizeof(decripted_word));
				munmap(shm_ptr, dim);
				exit(0);
			}
		}
		
		// astept sa se termine toate procesele
		for (int i = 0; i < num_words; i ++) {
			wait(NULL);
		}
		
		// creez fisierul decripted.txt in care o sa scriu cuvintele decriptate
		FILE *decr_ptr = fopen("decripted.txt", "w");
		
		// incarc cuvintele decriptate
		void *shm_ptr = mmap(0, num_words * dim, PROT_READ, MAP_SHARED, shm_fd, 0);
			
		if (shm_ptr == MAP_FAILED) {
			perror("shm_ptr parent\n");
			shm_unlink(shm_name);
			return errno ;
		}
		
		// scriu cuvintele in decripted.txt
		char buffer[MAX_BUFFER];
		for (int i = 0; i < num_words; i ++) {
			int p = 0;
			strncpy(buffer, shm_ptr, MAX_BUFFER - 1);
			shm_ptr += dim;
			fprintf(decr_ptr, "%s\n", buffer);
		}
		
		shm_ptr -= (num_words) * dim;
		munmap(shm_ptr, shm_size);
		shm_unlink(shm_name);
		fclose(decr_ptr);
		munmap(perms, sb2.st_size);
		close(perms_fd);
	}
	
	munmap(words, sb.st_size);
	close(words_fd);
	
	return 0;
}
