/**
 * Code inspired by RadNi code on github
 * https://gist.github.com/RadNi/9d8a074e6264c1664b97b8eee11b1d2a
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/fs.h> 
#include <asm/atomic.h>
#include <asm/uaccess.h>

#define LFS_MAGIC 0x19920342
#define TMPSIZE 20

//MODULE_LICENCE("GPL");
MODULE_AUTHOR("Miller Monteiro and Rodrigo Andrade");
MODULE_DESCRIPTION("The implementation of a Linux File System");
MODULE_VERSION("0.1");

/**
 * Representação de um arquivo usando um inode
 */
static struct inode *lfs_make_inode(struct super_block *sb, int mode, const struct file_operations* fops)
{
	struct inode* inode;            
        inode = new_inode(sb);
       if (!inode) {
                return NULL;
        }
        inode->i_mode = mode;
        inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
        inode->i_fop = fops;
        inode->i_ino = get_next_ino();
	return inode;

}


/**
 * Função que realiza a associação de um ponteiro, para a nossa
 * estrutura inode para que possa se abrir um arquivo
 */
static int lfs_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

/**
 * Função que realiza a leiura de um arquivo, são utilizados contadores
 * atomicos para evitar problemas de sincronização
 */

static ssize_t lfs_read_file(struct file *filp, char *buf, size_t count, loff_t *offset)
{
	atomic_t *counter = (atomic_t *) filp->private_data;
	int v, len;
	char tmp[TMPSIZE];
    /**
    * Fazer a leitura de partes do arquivo para analizar se ele é seguro
    * para fazer a leitura
    */
	v = atomic_read(counter);
	if (*offset > 0)
		v -= 1;
	else
		atomic_inc(counter);
	len = snprintf(tmp, TMPSIZE, "%d\n", v);
	if (*offset > len)
		return 0;
	if (count > len - *offset)
		count = len - *offset;
    /**
    * Retorna o resultado da leitura para o usuário e incrementa o contador indicando
    * que houve uma operação e que foi finalizada
    */
	if (copy_to_user(buf, tmp + *offset, count))
		return -EFAULT;
	*offset += count;
	return count;
}

/**
 * Função que realiza a escrita em um arquivo, essa função lê
 * o que o usuário inseriu e atualiza o arquivo a partir do começo do arquivo
 */
static ssize_t lfs_write_file(struct file *filp, const char *buf,size_t count, loff_t *offset)
{
	atomic_t *counter = (atomic_t *) filp->private_data;
	char tmp[TMPSIZE];
	if (*offset != 0)
		return -EINVAL;
	if (count >= TMPSIZE)
		return -EINVAL;
	memset(tmp, 0, TMPSIZE);
	if (copy_from_user(tmp, buf, count))
		return -EFAULT;
	atomic_set(counter, simple_strtol(tmp, NULL, 10));
	return count;
}


/**
 * Realizando a associação das operações que nosso sistema de arquivos será
 * capaz de realizar, por meio de reaproveitamento das funções prontas da
 * biblioteca linux/fs
 */
static struct file_operations lfs_file_ops = {
	.open	= lfs_open,
	.read 	= lfs_read_file,
	.write  = lfs_write_file,
};


/**
 * Mapeador de arquivos
 */
const struct inode_operations lwfs_inode_operations = {
    .setattr        = simple_setattr,
    .getattr        = simple_getattr,
};

/**
 * Cria um arquivo em um diretório, essa função faz o serviço pesado de 
 * associar um arquivo a um diretório
 */
static struct dentry *lfs_create_file (struct super_block *sb, struct dentry *dir, const char *name, atomic_t *counter)
{
	struct dentry *dentry;
	struct inode *inode;

    /**
     * Alocando espaços, a criação só será
     * efetivada caso, não tenha acontecido nenhum problema com a criação
     * desses espaços.
     */
	dentry = d_alloc_name(dir, name);
	if (! dentry)
		goto out;
	inode = lfs_make_inode(sb, S_IFREG | 0644, &lfs_file_ops);
	if (! inode)
		goto out_dput;
	inode->i_private = counter;
    /**
    * Criação realizada com sucesso, agora basta associar com o dentry
    * e verificar se a operação foi realizada com sucesso
    */
	d_add(dentry, inode);
	return dentry;

    out_dput:
        dput(dentry);
    out:
        return 0;
}


/**
 * Criando o diretório para armazenar arquivos
 */
static struct dentry *lfs_create_dir (struct super_block *sb, struct dentry *parent, const char *name)
{
	struct dentry *dentry;
	struct inode *inode;

	dentry = d_alloc_name(parent, name);
	if (! dentry)
		goto out;

	inode = lfs_make_inode(sb, S_IFDIR | 0755, &simple_dir_operations);
	if (! inode)
		goto out_dput;
	inode->i_op = &simple_dir_inode_operations;

	d_add(dentry, inode);
	return dentry;

  out_dput:
	dput(dentry);
  out:
	return 0;
}

/**
 * Função que cria um arquivo contador no diretório root e em outro
 * subdiretório 
 */
static atomic_t counter, subcounter;

static void lfs_create_files (struct super_block *sb, struct dentry *root)
{
	struct dentry *subdir;
    /*
    * Criando contador no root
    */
	atomic_set(&counter, 0);
	lfs_create_file(sb, root, "counter", &counter);
    /*
    * E no subdiretório
    */
	atomic_set(&subcounter, 0);
	subdir = lfs_create_dir(sb, root, "subdir");
	if (subdir)
		lfs_create_file(sb, subdir, "subcounter", &subcounter);
}



/**
 * Criação de suberbloco de memória que indica a camada virtual de sistema
 * de arquivo indicação sobre como o sistema de arquivo funciona e o que ele
 * precisa.
 */

/**
 * Operações genéricas do superbloco, já implementadas nas bilbiotecas importadas
 */
static struct super_operations lfs_s_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
};

/**
 * Alocando espaço para o superbloco e preenchendo com informações por exemplo
 * o usuário root
 */
static int lfs_fill_super (struct super_block *sb, void *data, int silent)
{
	struct inode *root;
	struct dentry *root_dentry;
    /*
    * Valores básicos da alocação do espaço do superbloco
    */
	sb->s_blocksize = VMACACHE_SIZE;
	sb->s_blocksize_bits = VMACACHE_SIZE;
	sb->s_magic = LFS_MAGIC;
	sb->s_op = &lfs_s_ops;
    /*
     * Usando Inode para fazer a representação do usuário root, todas as 
     * operações de root já são implementadas, logo ao criar o root, não
     * é necessário criar nenhuma função específica para o mesmo.
    */
	root = lfs_make_inode (sb, S_IFDIR | 0755, &simple_dir_operations);
	inode_init_owner(root, NULL, S_IFDIR | 0755);
	if (! root)
		goto out;
	root->i_op = &simple_dir_inode_operations;
//	root->i_fop = &simple_dir_operations;
    /*
    * Usando dentry para representar o diretório para o root
    */
	set_nlink(root, 2);
	root_dentry = d_make_root(root);
	if (! root_dentry)
		goto out_iput;
    /*
    * Criando os arquivos que serão do sistema de arquivos
    */
	lfs_create_files (sb, root_dentry);
	sb->s_root = root_dentry;
	return 0;
	
    out_iput:
        iput(root);
    out:
	    return -ENOMEM;
}


/*
 * Dados para registrar o sistema de arquivos e poder fazer o mount do mesmo.
 */
static struct dentry *lfs_get_super(struct file_system_type *fst, int flags, const char *devname, void *data)
{
	return mount_nodev(fst, flags, data, lfs_fill_super);
}

static struct file_system_type lfs_type = {
	.owner 		= THIS_MODULE,
	.name		= "enigma",
	.mount		= lfs_get_super,
	.kill_sb	= kill_litter_super,
};


/*
 * Iniciando o módulo
 */
static int __init lfs_init(void)
{
	return register_filesystem(&lfs_type);
}

/**
 * Finalizando o módulo
 */
static void __exit lfs_exit(void)
{
	unregister_filesystem(&lfs_type);
}

module_init(lfs_init);
module_exit(lfs_exit);