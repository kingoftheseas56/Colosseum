// The indexed publication helper must retain the packed source's scan order
// for score/count ties. Helper indexes are for lookup speed, not new rankings.
export function publicationSql(sql){
 return sql.replace(/FROM lib_mal_tag_count\b/g,'FROM lib_mal_tag_count NOT INDEXED')
           .replace(/FROM den_mal_anime( r)?\b/g,'FROM den_mal_anime$1 NOT INDEXED');
}
